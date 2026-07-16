/*
 * Copyright (c) 2026 The zmk-iidx contributors
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_iidx_boot_select

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk_iidx_hid, CONFIG_ZMK_LOG_LEVEL);

struct behavior_iidx_boot_select_config {
    struct zmk_behavior_binding mode_binding;
    struct zmk_behavior_binding fallback_binding;
    int32_t boot_timeout_ms;
};

struct behavior_iidx_boot_select_data {
    const struct zmk_behavior_binding *pressed_binding;
};

static int on_pressed(struct zmk_behavior_binding *binding,
                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_iidx_boot_select_config *config = dev->config;
    struct behavior_iidx_boot_select_data *data = dev->data;

    if (data->pressed_binding != NULL) {
        LOG_ERR("Boot-select key pressed twice without a release");
        return -ENOTSUP;
    }

    bool in_boot_window = event.timestamp >= 0 && event.timestamp <= config->boot_timeout_ms;
    data->pressed_binding = in_boot_window ? &config->mode_binding : &config->fallback_binding;

    return zmk_behavior_invoke_binding(data->pressed_binding, event, true);
}

static int on_released(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_iidx_boot_select_data *data = dev->data;

    if (data->pressed_binding == NULL) {
        LOG_WRN("Boot-select key released without a matching press");
        return ZMK_BEHAVIOR_OPAQUE;
    }

    const struct zmk_behavior_binding *pressed_binding = data->pressed_binding;
    data->pressed_binding = NULL;
    return zmk_behavior_invoke_binding(pressed_binding, event, false);
}

static const struct behavior_driver_api behavior_iidx_boot_select_driver_api = {
    .binding_pressed = on_pressed,
    .binding_released = on_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

#define IIDX_BOOT_SELECT_INST(n)                                                                  \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, bindings) == 2,                                              \
                 "IIDX boot-select requires exactly two bindings");                               \
    BUILD_ASSERT(DT_INST_PROP(n, boot_timeout_ms) >= 0,                                           \
                 "IIDX boot-select timeout must not be negative");                                \
    static const struct behavior_iidx_boot_select_config behavior_iidx_boot_select_config_##n = { \
        .mode_binding = ZMK_KEYMAP_EXTRACT_BINDING(0, DT_DRV_INST(n)),                            \
        .fallback_binding = ZMK_KEYMAP_EXTRACT_BINDING(1, DT_DRV_INST(n)),                        \
        .boot_timeout_ms = DT_INST_PROP(n, boot_timeout_ms),                                      \
    };                                                                                            \
    static struct behavior_iidx_boot_select_data behavior_iidx_boot_select_data_##n;              \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &behavior_iidx_boot_select_data_##n,                   \
                            &behavior_iidx_boot_select_config_##n, POST_KERNEL,                   \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                  \
                            &behavior_iidx_boot_select_driver_api);

DT_INST_FOREACH_STATUS_OKAY(IIDX_BOOT_SELECT_INST)
