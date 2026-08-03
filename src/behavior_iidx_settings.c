/*
 * Copyright (c) 2026 The zmk-iidx contributors
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_iidx_settings

#include <errno.h>
#include <stdio.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk_iidx/settings.h>

#include <zmk/behavior.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>
#include <zmk_iidx/settings.h>

LOG_MODULE_DECLARE(zmk_iidx_hid, CONFIG_ZMK_LOG_LEVEL);

#define SETTINGS_SCREEN_MAX 128
#define SETTINGS_LAYER_EXIT_TARGET 1

enum render_phase {
    RENDER_SELECT_ALL,
    RENDER_CLEAR,
    RENDER_TYPE,
};

static struct {
    bool running;
    bool render_again;
    enum render_phase phase;
    size_t text_index;
    char text[SETTINGS_SCREEN_MAX];
    struct k_work_delayable work;
} render_state;

static inline void press(uint32_t keycode) {
    raise_zmk_keycode_state_changed_from_encoded(keycode, true, (uint32_t)k_uptime_get());
}

static inline void release(uint32_t keycode) {
    raise_zmk_keycode_state_changed_from_encoded(keycode, false, (uint32_t)k_uptime_get());
}

static inline void tap(uint32_t keycode) {
    press(keycode);
    k_msleep(1);
    release(keycode);
}

static void tap_with_mod(uint32_t mod, uint32_t keycode) {
    press(mod);
    k_msleep(1);
    tap(keycode);
    k_msleep(1);
    release(mod);
}

static bool char_to_keycode(char c, uint32_t *keycode) {
    if (c >= 'a' && c <= 'z') {
        static const uint32_t letters[] = {
            A, B, C, D, E, F, G, H, I, J, K, L, M,
            N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
        };
        *keycode = letters[c - 'a'];
        return true;
    }

    if (c >= '1' && c <= '9') {
        static const uint32_t digits[] = {N1, N2, N3, N4, N5, N6, N7, N8, N9};
        *keycode = digits[c - '1'];
        return true;
    }

    switch (c) {
    case '0':
        *keycode = N0;
        return true;
    case ' ':
        *keycode = SPACE;
        return true;
    case '\n':
        *keycode = ENTER;
        return true;
    default:
        return false;
    }
}

static uint32_t character_delay(char c) { return c == '\n' ? 25 : 6; }

static void begin_render(void) {
    int32_t sensitivity = zmk_iidx_settings_get_scratch_sensitivity();
    snprintf(render_state.text, sizeof(render_state.text),
             "iidx scratch setting\nsensitivity %d\nb1 down b2 up\n"
             "b3 reset b4 show b5 exit\n",
             (int)sensitivity);
    render_state.phase = RENDER_SELECT_ALL;
    render_state.text_index = 0;
    render_state.running = true;
    k_work_reschedule(&render_state.work, K_NO_WAIT);
}

static void request_render(void) {
    if (render_state.running) {
        render_state.render_again = true;
        return;
    }

    begin_render();
}

static void render_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    if (!render_state.running) {
        return;
    }

    switch (render_state.phase) {
    case RENDER_SELECT_ALL:
        tap_with_mod(LCTRL, A);
        render_state.phase = RENDER_CLEAR;
        k_work_reschedule(&render_state.work, K_MSEC(18));
        return;
    case RENDER_CLEAR:
        tap(BACKSPACE);
        render_state.phase = RENDER_TYPE;
        k_work_reschedule(&render_state.work, K_MSEC(18));
        return;
    case RENDER_TYPE: {
        char c = render_state.text[render_state.text_index];
        if (c == '\0') {
            render_state.running = false;
            if (render_state.render_again) {
                render_state.render_again = false;
                begin_render();
            }
            return;
        }

        uint32_t keycode;
        if (char_to_keycode(c, &keycode)) {
            tap(keycode);
        }
        render_state.text_index++;
        k_work_reschedule(&render_state.work, K_MSEC(character_delay(c)));
        return;
    }
    }
}

static int on_pressed(struct zmk_behavior_binding *binding,
                      struct zmk_behavior_binding_event event) {
    ARG_UNUSED(event);

    switch (binding->param1) {
    case IIDX_SETTING_SHOW:
        request_render();
        break;
    case IIDX_SETTING_SENSITIVITY_DOWN:
        zmk_iidx_settings_adjust_scratch_sensitivity(-1);
        request_render();
        break;
    case IIDX_SETTING_SENSITIVITY_UP:
        zmk_iidx_settings_adjust_scratch_sensitivity(1);
        request_render();
        break;
    case IIDX_SETTING_SENSITIVITY_RESET:
        zmk_iidx_settings_set_scratch_sensitivity(ZMK_IIDX_SCRATCH_SENSITIVITY_DEFAULT);
        request_render();
        break;
    case IIDX_SETTING_EXIT:
        render_state.running = false;
        render_state.render_again = false;
        k_work_cancel_delayable(&render_state.work);
        if (zmk_iidx_settings_save() < 0) {
            LOG_WRN("Failed to save IIDX settings");
        }
        zmk_keymap_layer_to(SETTINGS_LAYER_EXIT_TARGET, false);
        break;
    default:
        return -EINVAL;
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_released(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event) {
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_iidx_settings_driver_api = {
    .binding_pressed = on_pressed,
    .binding_released = on_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .get_parameter_metadata = zmk_behavior_get_empty_param_metadata,
#endif
};

static int behavior_iidx_settings_init(const struct device *dev) {
    ARG_UNUSED(dev);
    k_work_init_delayable(&render_state.work, render_work_handler);
    return 0;
}

#define IIDX_SETTINGS_INST(n)                                                                    \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_iidx_settings_init, NULL, NULL, NULL, POST_KERNEL,       \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                 \
                            &behavior_iidx_settings_driver_api);

DT_INST_FOREACH_STATUS_OKAY(IIDX_SETTINGS_INST)
