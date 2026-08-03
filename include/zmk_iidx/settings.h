/*
 * Copyright (c) 2026 The zmk-iidx contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#define ZMK_IIDX_SCRATCH_SENSITIVITY_DEFAULT 5
#define ZMK_IIDX_SCRATCH_SENSITIVITY_MIN 1
#define ZMK_IIDX_SCRATCH_SENSITIVITY_MAX 20

int32_t zmk_iidx_settings_get_scratch_sensitivity(void);
int32_t zmk_iidx_settings_set_scratch_sensitivity(int32_t value);
int32_t zmk_iidx_settings_adjust_scratch_sensitivity(int32_t delta);
int zmk_iidx_settings_save(void);
