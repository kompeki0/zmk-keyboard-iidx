/*
 * Copyright (c) 2026 The zmk-iidx contributors
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>

#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <zmk/ble.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#include <zmk_iidx/ble.h>
#include <zmk_iidx/hid.h>
#include <zmk_iidx/mode.h>

LOG_MODULE_DECLARE(zmk_iidx_hid, CONFIG_ZMK_LOG_LEVEL);

#define IIDX_BLE_SERVICE_UUID BT_UUID_DECLARE_16(0xFF00)
#define IIDX_BLE_KEY_INPUT_UUID BT_UUID_DECLARE_16(0xFF01)
#define IIDX_BLE_UNKNOWN_2_UUID BT_UUID_DECLARE_16(0xFF02)
#define IIDX_BLE_UNKNOWN_3_UUID BT_UUID_DECLARE_16(0xFF03)

#define IIDX_BLE_KEY_INPUT_ATTR_INDEX 1
#define IIDX_BLE_UNKNOWN_3_ATTR_INDEX 6

static atomic_t notifications_enabled;
static atomic_t unknown_3_notifications_enabled;
static uint8_t sequence = 1;

static void iidx_ble_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value);
static void iidx_ble_unknown_3_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value);

static ssize_t iidx_ble_write_ignored(struct bt_conn *conn, const struct bt_gatt_attr *attr,
                                      const void *buf, uint16_t len, uint16_t offset,
                                      uint8_t flags) {
    ARG_UNUSED(conn);
    ARG_UNUSED(attr);
    ARG_UNUSED(buf);
    ARG_UNUSED(offset);
    ARG_UNUSED(flags);

    return len;
}

BT_GATT_SERVICE_DEFINE(
    iidx_ble_svc, BT_GATT_PRIMARY_SERVICE(IIDX_BLE_SERVICE_UUID),
    BT_GATT_CHARACTERISTIC(IIDX_BLE_KEY_INPUT_UUID, BT_GATT_CHRC_NOTIFY, BT_GATT_PERM_NONE,
                           NULL, NULL, NULL),
    BT_GATT_CCC(iidx_ble_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE),
    BT_GATT_CHARACTERISTIC(IIDX_BLE_UNKNOWN_2_UUID,
                           BT_GATT_CHRC_WRITE | BT_GATT_CHRC_WRITE_WITHOUT_RESP,
                           BT_GATT_PERM_WRITE, NULL, iidx_ble_write_ignored, NULL),
    BT_GATT_CHARACTERISTIC(IIDX_BLE_UNKNOWN_3_UUID, BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_WRITE,
                           BT_GATT_PERM_WRITE, NULL, iidx_ble_write_ignored, NULL),
    BT_GATT_CCC(iidx_ble_unknown_3_ccc_changed, BT_GATT_PERM_READ | BT_GATT_PERM_WRITE));

static bool should_notify(void) {
    return atomic_get(&notifications_enabled) != 0 && zmk_iidx_mode_is_active() &&
           !zmk_usb_is_hid_ready();
}

static void notify_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (!should_notify()) {
        return;
    }

    struct zmk_iidx_hid_report report;
    zmk_iidx_hid_get_report(&report);

    uint8_t buttons = (uint8_t)(report.keys_1_7 & 0x7F);
    uint8_t effectors = (uint8_t)(report.keys_e1_e4 & 0x03);
    uint8_t packet[10] = {
        (uint8_t)report.x,
        0x00,
        buttons,
        effectors,
        sequence,
        (uint8_t)report.x,
        0x00,
        buttons,
        effectors,
        (uint8_t)(sequence + 1),
    };

    int err = bt_gatt_notify(NULL, &iidx_ble_svc.attrs[IIDX_BLE_KEY_INPUT_ATTR_INDEX], packet,
                             sizeof(packet));
    if (err != 0 && err != -ENOTCONN) {
        LOG_DBG("Failed IIDX BLE input notification: %d", err);
    }

    sequence++;
}

K_WORK_DEFINE(notify_work, notify_work_handler);

static void unknown_3_notify_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (atomic_get(&unknown_3_notifications_enabled) == 0) {
        return;
    }

    static const uint8_t initial_value[] = {0x01, 0x05};
    int err = bt_gatt_notify(NULL, &iidx_ble_svc.attrs[IIDX_BLE_UNKNOWN_3_ATTR_INDEX],
                             initial_value, sizeof(initial_value));
    if (err != 0 && err != -ENOTCONN) {
        LOG_DBG("Failed FF03 initial notification: %d", err);
    }
}

K_WORK_DEFINE(unknown_3_notify_work, unknown_3_notify_work_handler);

static void notify_timer_handler(struct k_timer *timer) {
    ARG_UNUSED(timer);
    k_work_submit(&notify_work);
}

K_TIMER_DEFINE(notify_timer, notify_timer_handler, NULL);

static void update_notify_timer(void) {
    if (should_notify()) {
        k_timer_start(&notify_timer, K_NO_WAIT,
                      K_USEC(CONFIG_ZMK_IIDX_BLE_FRAME_INTERVAL_US));
    } else {
        k_timer_stop(&notify_timer);
    }
}

static void request_low_latency_connection(void) {
    struct bt_conn *conn = zmk_ble_active_profile_conn();
    if (conn == NULL) {
        return;
    }

    int err = bt_conn_le_param_update(
        conn, BT_LE_CONN_PARAM(CONFIG_ZMK_IIDX_BLE_CONN_INTERVAL,
                              CONFIG_ZMK_IIDX_BLE_CONN_INTERVAL, 0,
                              CONFIG_ZMK_IIDX_BLE_CONN_TIMEOUT));
    if (err != 0) {
        LOG_WRN("Failed to request IIDX BLE connection parameters: %d", err);
    }

    bt_conn_unref(conn);
}

static void iidx_ble_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    ARG_UNUSED(attr);

    bool enabled = value == BT_GATT_CCC_NOTIFY;
    atomic_set(&notifications_enabled, enabled);

    LOG_INF("IIDX BLE notifications %s", enabled ? "enabled" : "disabled");
    if (enabled) {
        request_low_latency_connection();
    }
    update_notify_timer();
}

static void iidx_ble_unknown_3_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    ARG_UNUSED(attr);

    bool enabled = value == BT_GATT_CCC_NOTIFY;
    atomic_set(&unknown_3_notifications_enabled, enabled);
    LOG_INF("IIDX BLE FF03 notifications %s", enabled ? "enabled" : "disabled");

    if (enabled) {
        k_work_submit(&unknown_3_notify_work);
    }
}

void zmk_iidx_ble_mode_changed(bool active) {
    ARG_UNUSED(active);
    update_notify_timer();
}

static int usb_conn_state_listener(const zmk_event_t *eh) {
    const struct zmk_usb_conn_state_changed *event = as_zmk_usb_conn_state_changed(eh);
    if (event == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (event->conn_state == ZMK_USB_CONN_HID) {
        struct zmk_endpoint_instance preferred = zmk_endpoint_get_preferred();
        if (preferred.transport != ZMK_TRANSPORT_USB) {
            int err = zmk_endpoint_set_preferred_transport(ZMK_TRANSPORT_USB);
            if (err != 0) {
                LOG_WRN("Failed to prefer USB transport: %d", err);
            }
        }
    }

    update_notify_timer();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(zmk_iidx_ble_usb, usb_conn_state_listener);
ZMK_SUBSCRIPTION(zmk_iidx_ble_usb, zmk_usb_conn_state_changed);
