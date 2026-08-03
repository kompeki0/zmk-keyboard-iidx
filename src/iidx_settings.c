/*
 * Copyright (c) 2026 The zmk-iidx contributors
 * SPDX-License-Identifier: MIT
 */

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

#include <zmk_iidx/settings.h>

#define IIDX_SETTINGS_SUBTREE "iidx"
#define IIDX_SCRATCH_SENSITIVITY_KEY "scratch_sensitivity"
#define IIDX_SCRATCH_SENSITIVITY_PATH                                                        \
    IIDX_SETTINGS_SUBTREE "/" IIDX_SCRATCH_SENSITIVITY_KEY

static atomic_t scratch_sensitivity = ZMK_IIDX_SCRATCH_SENSITIVITY_DEFAULT;

#if IS_ENABLED(CONFIG_SETTINGS)
static void save_work_handler(struct k_work *work) {
    ARG_UNUSED(work);

    int32_t value = atomic_get(&scratch_sensitivity);
    settings_save_one(IIDX_SCRATCH_SENSITIVITY_PATH, &value, sizeof(value));
}

K_WORK_DELAYABLE_DEFINE(save_work, save_work_handler);
#endif

int32_t zmk_iidx_settings_get_scratch_sensitivity(void) {
    return atomic_get(&scratch_sensitivity);
}

int32_t zmk_iidx_settings_set_scratch_sensitivity(int32_t value) {
    value = CLAMP(value, ZMK_IIDX_SCRATCH_SENSITIVITY_MIN,
                  ZMK_IIDX_SCRATCH_SENSITIVITY_MAX);
    atomic_set(&scratch_sensitivity, value);
#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_reschedule(&save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
#endif
    return value;
}

int32_t zmk_iidx_settings_adjust_scratch_sensitivity(int32_t delta) {
    return zmk_iidx_settings_set_scratch_sensitivity(
        zmk_iidx_settings_get_scratch_sensitivity() + delta);
}

int zmk_iidx_settings_save(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_cancel_delayable(&save_work);
    int32_t value = atomic_get(&scratch_sensitivity);
    return settings_save_one(IIDX_SCRATCH_SENSITIVITY_PATH, &value, sizeof(value));
#else
    return 0;
#endif
}

#if IS_ENABLED(CONFIG_SETTINGS)
static int settings_set(const char *name, size_t len, settings_read_cb read_cb, void *cb_arg) {
    if (!settings_name_steq(name, IIDX_SCRATCH_SENSITIVITY_KEY, NULL)) {
        return 0;
    }

    if (len != sizeof(int32_t)) {
        return -EINVAL;
    }

    int32_t value;
    ssize_t rc = read_cb(cb_arg, &value, sizeof(value));
    if (rc < 0) {
        return (int)rc;
    }
    if (rc != sizeof(value)) {
        return -EINVAL;
    }

    atomic_set(&scratch_sensitivity,
               CLAMP(value, ZMK_IIDX_SCRATCH_SENSITIVITY_MIN,
                     ZMK_IIDX_SCRATCH_SENSITIVITY_MAX));
    return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(zmk_iidx, IIDX_SETTINGS_SUBTREE, NULL, settings_set, NULL, NULL);
#endif
