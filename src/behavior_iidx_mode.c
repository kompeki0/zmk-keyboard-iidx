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

#define LAYER_PROP_ELEM(idx, inst, prop) DT_INST_PROP_BY_IDX(inst, prop, idx)
#define LAYER_PROP_LIST(inst, prop) LISTIFY(DT_INST_PROP_LEN(inst, prop), LAYER_PROP_ELEM, (,), inst, prop)
#define IIDX_LAYER_COUNT(inst)                                                                    \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, iidx_layers), (DT_INST_PROP_LEN(inst, iidx_layers)), \
                (COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, iidx_layer), (1), (0))))
#define KEYBOARD_LAYER_COUNT(inst)                                                                     \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, keyboard_layers), (DT_INST_PROP_LEN(inst, keyboard_layers)), \
                (COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, keyboard_layer), (1), (0))))
#define IIDX_LAYER_INIT(inst)                                                                         \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, iidx_layers), ((zmk_keymap_layer_id_t[]){LAYER_PROP_LIST(inst, iidx_layers)}), \
                (COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, iidx_layer), ((zmk_keymap_layer_id_t[]){DT_INST_PROP(inst, iidx_layer)}), \
                              ((zmk_keymap_layer_id_t[]){0}))))
#define KEYBOARD_LAYER_INIT(inst)                                                                          \
    COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, keyboard_layers), ((zmk_keymap_layer_id_t[]){LAYER_PROP_LIST(inst, keyboard_layers)}), \
                (COND_CODE_1(DT_INST_NODE_HAS_PROP(inst, keyboard_layer), ((zmk_keymap_layer_id_t[]){DT_INST_PROP(inst, keyboard_layer)}), \
                              ((zmk_keymap_layer_id_t[]){0}))))

struct behavior_iidx_mode_config {
    const zmk_keymap_layer_id_t *iidx_layers;
    size_t iidx_layer_count;
    const zmk_keymap_layer_id_t *keyboard_layers;
    size_t keyboard_layer_count;
    bool default_iidx_mode;
};

static const zmk_keymap_layer_id_t *active_iidx_layers;
static size_t active_iidx_layer_count;

static bool layer_in_set(zmk_keymap_layer_id_t layer, const zmk_keymap_layer_id_t *layers,
                         size_t count) {
    for (size_t i = 0; i < count; i++) {
        if (layers[i] == layer) {
            return true;
        }
    }

    return false;
}

bool zmk_iidx_mode_is_active(void) {
    if (active_iidx_layers == NULL || active_iidx_layer_count == 0) {
        return false;
    }

    zmk_keymap_layer_index_t top_index = zmk_keymap_highest_layer_active();
    zmk_keymap_layer_id_t top_layer = zmk_keymap_layer_index_to_id(top_index);
    return layer_in_set(top_layer, active_iidx_layers, active_iidx_layer_count);
}

static int apply_layer_set(const zmk_keymap_layer_id_t *layers, size_t count) {
    int err = 0;

    for (zmk_keymap_layer_id_t i = 0; i < ZMK_KEYMAP_LAYERS_LEN; i++) {
        int rc = zmk_keymap_layer_deactivate(i, false);
        if (rc < 0 && err == 0) {
            err = rc;
        }
    }

    for (size_t i = 0; i < count; i++) {
        int rc = zmk_keymap_layer_activate(layers[i], false);
        if (rc < 0 && err == 0) {
            err = rc;
        }
    }

    return err;
}

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

    const zmk_keymap_layer_id_t *layers =
        enable_iidx ? config->iidx_layers : config->keyboard_layers;
    size_t layer_count = enable_iidx ? config->iidx_layer_count : config->keyboard_layer_count;

    LOG_INF("Switching to %s mode with %u layer(s)", enable_iidx ? "IIDX" : "keyboard",
            (unsigned int)layer_count);
    err = apply_layer_set(layers, layer_count);
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

static int behavior_iidx_mode_init(const struct device *dev) {
    const struct behavior_iidx_mode_config *config = dev->config;
    active_iidx_layers = config->iidx_layers;
    active_iidx_layer_count = config->iidx_layer_count;

    if (config->default_iidx_mode) {
        return apply_layer_set(config->iidx_layers, config->iidx_layer_count);
    }

    return 0;
}

#define IIDX_MODE_INST(n)                                                                         \
    BUILD_ASSERT(IIDX_LAYER_COUNT(n) > 0,                                                         \
                 "IIDX mode requires at least one iidx-layer or iidx-layers entry");              \
    static const struct behavior_iidx_mode_config behavior_iidx_mode_config_##n = {              \
        .iidx_layers = IIDX_LAYER_INIT(n),                                                        \
        .iidx_layer_count = IIDX_LAYER_COUNT(n),                                                  \
        .keyboard_layers = KEYBOARD_LAYER_INIT(n),                                                \
        .keyboard_layer_count = KEYBOARD_LAYER_COUNT(n),                                          \
        .default_iidx_mode = DT_INST_PROP_OR(n, default_iidx_mode, 0),                           \
    };                                                                                            \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_iidx_mode_init, NULL, NULL,                              \
                            &behavior_iidx_mode_config_##n, POST_KERNEL,                          \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                  \
                            &behavior_iidx_mode_driver_api);

DT_INST_FOREACH_STATUS_OKAY(IIDX_MODE_INST)
