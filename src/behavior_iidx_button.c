/*
 * Copyright (c) 2026 The zmk-iidx contributors
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_iidx_button

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk_iidx/hid.h>
#include <zmk_iidx/mode.h>

LOG_MODULE_DECLARE(zmk_iidx_hid, CONFIG_ZMK_LOG_LEVEL);

static int update_button(struct zmk_behavior_binding *binding, bool pressed) {
    if (!zmk_iidx_mode_is_active()) {
        return ZMK_BEHAVIOR_OPAQUE;
    }

    if (binding->param1 > UINT8_MAX) {
        LOG_WRN("Invalid IIDX button bit %u", binding->param1);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    int err = zmk_iidx_hid_set_button((uint8_t)binding->param1, pressed);
    if (err != 0 && err != -ENODEV && err != -EAGAIN) {
        LOG_WRN("Failed to send IIDX button bit %u: %d", binding->param1, err);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_pressed(struct zmk_behavior_binding *binding,
                      struct zmk_behavior_binding_event event) {
    return update_button(binding, true);
}

static int on_released(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    return update_button(binding, false);
}

static const struct behavior_driver_api behavior_iidx_button_driver_api = {
    .binding_pressed = on_pressed,
    .binding_released = on_released,
};

#define IIDX_BUTTON_INST(n)                                                                       \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, NULL, NULL, POST_KERNEL,                               \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                  \
                            &behavior_iidx_button_driver_api);

DT_INST_FOREACH_STATUS_OKAY(IIDX_BUTTON_INST)
