/*
 * Copyright (c) 2026 The zmk-iidx contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

/** Start or stop vendor notifications as the active IIDX mode changes. */
void zmk_iidx_ble_mode_changed(bool active);
