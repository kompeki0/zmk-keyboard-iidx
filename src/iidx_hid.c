/*
 * Copyright (c) 2026 The zmk-iidx contributors
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zephyr/usb/class/usb_hid.h>
#include <zephyr/usb/usb_device.h>

#include <zmk/usb.h>
#include <zmk_iidx/hid.h>
#include <zmk_iidx/mode.h>

LOG_MODULE_REGISTER(zmk_iidx_hid, CONFIG_ZMK_LOG_LEVEL);

static const uint8_t iidx_report_descriptor[] = {
    0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0x09, 0x01, 0xA1, 0x00, 0x05, 0x01, 0x09,
    0x30, 0x09, 0x31, 0x15, 0x81, 0x25, 0x7F, 0x75, 0x08, 0x95, 0x02, 0x81, 0x02,
    0x05, 0x09, 0x19, 0x01, 0x29, 0x10, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95,
    0x10, 0x81, 0x02, 0x75, 0x08, 0x95, 0x01, 0x81, 0x03, 0xC0, 0xC0,
};

BUILD_ASSERT(sizeof(iidx_report_descriptor) == 50, "IIDX report descriptor must be 50 bytes");
BUILD_ASSERT(sizeof(struct zmk_iidx_hid_report) == 5, "IIDX input report must be 5 bytes");
BUILD_ASSERT(CONFIG_USB_HID_DEVICE_COUNT == 2,
             "Composite keyboard/IIDX mode requires exactly two HID devices");

static const struct device *hid_dev;
static struct zmk_iidx_hid_report current_report;
static struct zmk_iidx_hid_report control_report;
static struct zmk_iidx_hid_report tx_report;
static K_MUTEX_DEFINE(report_mutex);
static K_SEM_DEFINE(endpoint_sem, 1, 1);

static int8_t clamp_axis(int32_t value) { return (int8_t)CLAMP(value, -127, 127); }
static bool valid_button_bit(uint8_t bit) { return bit <= 6 || (bit >= 8 && bit <= 11); }

static void sanitize_report(struct zmk_iidx_hid_report *report) {
    report->keys_1_7 &= 0x7F;
    report->keys_e1_e4 &= 0x0F;
    report->reserved = 0;
}

static void in_ready_cb(const struct device *dev) { k_sem_give(&endpoint_sem); }

#define HID_REPORT_TYPE_MASK 0xFF00
#define HID_REPORT_ID_MASK 0x00FF
#define HID_REPORT_TYPE_INPUT 0x0100

static int get_report_cb(const struct device *dev, struct usb_setup_packet *setup, int32_t *len,
                         uint8_t **data) {
    if ((setup->wValue & HID_REPORT_TYPE_MASK) != HID_REPORT_TYPE_INPUT ||
        (setup->wValue & HID_REPORT_ID_MASK) != 0) {
        return -ENOTSUP;
    }

    k_mutex_lock(&report_mutex, K_FOREVER);
    control_report = current_report;
    sanitize_report(&control_report);
    k_mutex_unlock(&report_mutex);

    *len = sizeof(control_report);
    *data = (uint8_t *)&control_report;
    return 0;
}

static const struct hid_ops hid_ops = {
    .int_in_ready = in_ready_cb,
    .get_report = get_report_cb,
};

int zmk_iidx_hid_send(void) {
    if (hid_dev == NULL) {
        return -ENODEV;
    }

    switch (zmk_usb_get_status()) {
    case USB_DC_SUSPEND:
        return usb_wakeup_request();
    case USB_DC_CONFIGURED:
    case USB_DC_RESUME:
    case USB_DC_SOF:
        break;
    default:
        return -ENODEV;
    }

    int err = k_sem_take(&endpoint_sem, K_MSEC(CONFIG_ZMK_IIDX_HID_SEND_TIMEOUT_MS));
    if (err != 0) {
        return err;
    }

    k_mutex_lock(&report_mutex, K_FOREVER);
    tx_report = current_report;
    sanitize_report(&tx_report);
    k_mutex_unlock(&report_mutex);

    err = hid_int_ep_write(hid_dev, (uint8_t *)&tx_report, sizeof(tx_report), NULL);
    if (err != 0) {
        k_sem_give(&endpoint_sem);
    }

    return err;
}

int zmk_iidx_hid_clear(void) {
    k_mutex_lock(&report_mutex, K_FOREVER);
    memset(&current_report, 0, sizeof(current_report));
    k_mutex_unlock(&report_mutex);

    return zmk_iidx_hid_send();
}

int zmk_iidx_hid_set_button(uint8_t bit, bool pressed) {
    if (!zmk_iidx_mode_is_active()) {
        return -EACCES;
    }

    if (!valid_button_bit(bit)) {
        return -EINVAL;
    }

    k_mutex_lock(&report_mutex, K_FOREVER);
    if (bit < 8) {
        WRITE_BIT(current_report.keys_1_7, bit, pressed);
    } else {
        WRITE_BIT(current_report.keys_e1_e4, bit - 8, pressed);
    }
    sanitize_report(&current_report);
    k_mutex_unlock(&report_mutex);

    return zmk_iidx_hid_send();
}

int zmk_iidx_hid_set_axis(enum zmk_iidx_axis axis, int16_t value) {
    if (!zmk_iidx_mode_is_active()) {
        return -EACCES;
    }

    k_mutex_lock(&report_mutex, K_FOREVER);
    switch (axis) {
    case ZMK_IIDX_AXIS_X:
        current_report.x = clamp_axis(value);
        break;
    case ZMK_IIDX_AXIS_Y:
        current_report.y = clamp_axis(value);
        break;
    default:
        k_mutex_unlock(&report_mutex);
        return -EINVAL;
    }
    k_mutex_unlock(&report_mutex);

    return zmk_iidx_hid_send();
}

int zmk_iidx_hid_adjust_axis(enum zmk_iidx_axis axis, int16_t delta) {
    if (!zmk_iidx_mode_is_active()) {
        return -EACCES;
    }

    k_mutex_lock(&report_mutex, K_FOREVER);
    switch (axis) {
    case ZMK_IIDX_AXIS_X:
        current_report.x = clamp_axis((int32_t)current_report.x + delta);
        break;
    case ZMK_IIDX_AXIS_Y:
        current_report.y = clamp_axis((int32_t)current_report.y + delta);
        break;
    default:
        k_mutex_unlock(&report_mutex);
        return -EINVAL;
    }
    k_mutex_unlock(&report_mutex);

    return zmk_iidx_hid_send();
}

int zmk_iidx_hid_set_axes(int16_t x, int16_t y) {
    if (!zmk_iidx_mode_is_active()) {
        return -EACCES;
    }

    k_mutex_lock(&report_mutex, K_FOREVER);
    current_report.x = clamp_axis(x);
    current_report.y = clamp_axis(y);
    k_mutex_unlock(&report_mutex);

    return zmk_iidx_hid_send();
}

static int zmk_iidx_hid_init(void) {
    hid_dev = device_get_binding("HID_1");
    if (hid_dev == NULL) {
        LOG_ERR("Unable to locate HID_1; set CONFIG_USB_HID_DEVICE_COUNT=2");
        return -ENODEV;
    }

    usb_hid_register_device(hid_dev, iidx_report_descriptor, sizeof(iidx_report_descriptor),
                            &hid_ops);
    usb_hid_init(hid_dev);
    LOG_INF("Registered 5-byte IIDX HID report on HID_1");
    return 0;
}

SYS_INIT(zmk_iidx_hid_init, APPLICATION, CONFIG_ZMK_IIDX_HID_INIT_PRIORITY);
