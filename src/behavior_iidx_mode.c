/*
 * Copyright (c) 2026 The zmk-iidx contributors
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_iidx_mode

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/atomic.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/keymap.h>
#include <zmk/usb_hid.h>
#include <zmk_iidx/hid.h>
#include <zmk_iidx/mode.h>

LOG_MODULE_DECLARE(zmk_iidx_hid, CONFIG_ZMK_LOG_LEVEL);

struct behavior_iidx_mode_config {
    zmk_keymap_layer_id_t iidx_layer;
    zmk_keymap_layer_id_t keyboard_layer;
};

static atomic_t iidx_mode_active;

bool zmk_iidx_mode_is_active(void) { return atomic_get(&iidx_mode_active) != 0; }

static void clear_standard_usb_reports(void) {
    zmk_hid_keyboard_clear();
    zmk_hid_consumer_clear();
#if IS_ENABLED(CONFIG_ZMK_POINTING)
    zmk_hid_mouse_clear();
#if IS_ENABLED(CONFIG_ZMK_POINTING_HID_TOUCHPAD)
    zmk_hid_touchpad_clear();
#endif
#endif

    zmk_usb_hid_send_keyboard_report();
    zmk_usb_hid_send_consumer_report();
#if IS_ENABLED(CONFIG_ZMK_POINTING)
    zmk_usb_hid_send_mouse_report();
#if IS_ENABLED(CONFIG_ZMK_POINTING_HID_TOUCHPAD)
    zmk_usb_hid_send_touchpad_report();
#endif
#endif
}

static int on_pressed(struct zmk_behavior_binding *binding,
                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_iidx_mode_config *config = dev->config;
    bool enable_iidx = !zmk_iidx_mode_is_active();

    int err = zmk_iidx_hid_clear();
    if (err != 0 && err != -ENODEV && err != -EAGAIN) {
        LOG_WRN("Failed to clear IIDX report: %d", err);
    }
    clear_standard_usb_reports();

    atomic_set(&iidx_mode_active, enable_iidx);
    zmk_keymap_layer_id_t layer = enable_iidx ? config->iidx_layer : config->keyboard_layer;

    LOG_INF("Switching to %s mode on layer %u", enable_iidx ? "IIDX" : "keyboard", layer);
    err = zmk_keymap_layer_to(layer, false);
    return err < 0 ? err : ZMK_BEHAVIOR_OPAQUE;
}

static int on_released(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_iidx_mode_driver_api = {
    .binding_pressed = on_pressed,
    .binding_released = on_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define IIDX_MODE_INST(n)                                                                         \
    static const struct behavior_iidx_mode_config behavior_iidx_mode_config_##n = {              \
        .iidx_layer = DT_INST_PROP(n, iidx_layer),                                                \
        .keyboard_layer = DT_INST_PROP(n, keyboard_layer),                                        \
    };                                                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, &behavior_iidx_mode_config_##n, POST_KERNEL,     \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                  \
                            &behavior_iidx_mode_driver_api);

DT_INST_FOREACH_STATUS_OKAY(IIDX_MODE_INST)
