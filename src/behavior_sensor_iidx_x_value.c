/*
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_sensor_iidx_x_value

#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>

#include <zmk/behavior.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/virtual_key_position.h>
#include <zmk_iidx/hid.h>

#include "zmk_value_store.h"

#ifndef ZMK_KEYMAP_SENSORS_LEN
#define ZMK_KEYMAP_SENSORS_LEN 0
#endif

#ifndef ZMK_KEYMAP_LAYERS_LEN
#define ZMK_KEYMAP_LAYERS_LEN 1
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

enum dir {
    DIR_NONE = 0,
    DIR_CW = 1,
    DIR_CCW = 2,
};

struct behavior_sensor_iidx_x_value_config {
    uint8_t index;
    int32_t min_v;
    int32_t max_v;
    int32_t step;
    uint16_t ticks_per_step;
    bool continuous;
};

struct value_adjust_state {
    int32_t tick_accum;
};

struct behavior_sensor_iidx_x_value_data {
    enum dir pending_dir[ZMK_KEYMAP_SENSORS_LEN][ZMK_KEYMAP_LAYERS_LEN];
    struct value_adjust_state st[ZMK_KEYMAP_SENSORS_LEN][ZMK_KEYMAP_LAYERS_LEN];
};

static inline int32_t clamp_i32(int32_t v, int32_t lo, int32_t hi) {
    return (v < lo) ? lo : (v > hi) ? hi : v;
}

static inline int32_t mod_pos_i32(int32_t a, int32_t m) {
    int32_t r = a % m;
    return (r < 0) ? (r + m) : r;
}

static int32_t wrap_i32(int32_t v, int32_t min_v, int32_t max_v) {
    int32_t range = max_v - min_v + 1;
    if (range <= 0) {
        return min_v;
    }

    return min_v + mod_pos_i32(v - min_v, range);
}

static int32_t value_store_add_wrapped(uint8_t index, int32_t delta, int32_t min_v, int32_t max_v,
                                       bool continuous, int32_t *out_newv) {
    int32_t cur = zmk_value_store_get(index, min_v);
    int32_t next = cur + delta;

    if (continuous) {
        next = wrap_i32(next, min_v, max_v);
    } else {
        next = clamp_i32(next, min_v, max_v);
    }

    zmk_value_store_set(index, next);

    if (out_newv) {
        *out_newv = next;
    }

    return next;
}

static int accept_data(struct zmk_behavior_binding *binding,
                       struct zmk_behavior_binding_event event,
                       const struct zmk_sensor_config *sensor_config,
                       size_t channel_data_size,
                       const struct zmk_sensor_channel_data *channel_data) {
    ARG_UNUSED(sensor_config);
    ARG_UNUSED(channel_data_size);

    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_sensor_iidx_x_value_data *data = dev->data;
    const int sensor_index = ZMK_SENSOR_POSITION_FROM_VIRTUAL_KEY_POSITION(event.position);

    const struct sensor_value v = channel_data[0].value;
    int delta = (v.val1 == 0) ? v.val2 : v.val1;

    if (delta > 0) {
        data->pending_dir[sensor_index][event.layer] = DIR_CW;
    } else if (delta < 0) {
        data->pending_dir[sensor_index][event.layer] = DIR_CCW;
    } else {
        data->pending_dir[sensor_index][event.layer] = DIR_NONE;
    }

    return 0;
}

static int process(struct zmk_behavior_binding *binding, struct zmk_behavior_binding_event event,
                   enum behavior_sensor_binding_process_mode mode) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_sensor_iidx_x_value_config *cfg = dev->config;
    struct behavior_sensor_iidx_x_value_data *data = dev->data;
    const int sensor_index = ZMK_SENSOR_POSITION_FROM_VIRTUAL_KEY_POSITION(event.position);

    if (mode != BEHAVIOR_SENSOR_BINDING_PROCESS_MODE_TRIGGER) {
        data->pending_dir[sensor_index][event.layer] = DIR_NONE;
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

    enum dir d = data->pending_dir[sensor_index][event.layer];
    data->pending_dir[sensor_index][event.layer] = DIR_NONE;

    if (d == DIR_NONE) {
        return ZMK_BEHAVIOR_TRANSPARENT;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT)
    event.source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL;
#endif

    struct value_adjust_state *st = &data->st[sensor_index][event.layer];
    uint16_t tps = cfg->ticks_per_step ? cfg->ticks_per_step : 1;

    if (d == DIR_CW) {
        st->tick_accum += 1;
    } else {
        st->tick_accum -= 1;
    }

    while (st->tick_accum >= (int32_t)tps) {
        st->tick_accum -= (int32_t)tps;

        int32_t newv = 0;
        value_store_add_wrapped(cfg->index, cfg->step, cfg->min_v, cfg->max_v, cfg->continuous,
                                &newv);
        int rc = zmk_iidx_hid_set_axis(ZMK_IIDX_AXIS_X, newv);
        LOG_DBG("value[%d] += %d -> %d rc=%d", cfg->index, (int)cfg->step, (int)newv, rc);
    }

    while (st->tick_accum <= -(int32_t)tps) {
        st->tick_accum += (int32_t)tps;

        int32_t newv = 0;
        value_store_add_wrapped(cfg->index, -cfg->step, cfg->min_v, cfg->max_v, cfg->continuous,
                                &newv);
        int rc = zmk_iidx_hid_set_axis(ZMK_IIDX_AXIS_X, newv);
        LOG_DBG("value[%d] -= %d -> %d rc=%d", cfg->index, (int)cfg->step, (int)newv, rc);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api api = {
    .sensor_binding_accept_data = accept_data,
    .sensor_binding_process = process,
};

#define INST(n)                                                                                    \
    static const struct behavior_sensor_iidx_x_value_config cfg_##n = {                            \
        .index = (uint8_t)DT_INST_PROP(n, index),                                                  \
        .min_v = DT_INST_PROP_OR(n, min, -127),                                                    \
        .max_v = DT_INST_PROP_OR(n, max, 127),                                                     \
        .step = DT_INST_PROP_OR(n, step, 1),                                                       \
        .ticks_per_step = DT_INST_PROP_OR(n, ticks_per_step, 1),                                   \
        .continuous = DT_INST_PROP_OR(n, continuous, 0),                                           \
    };                                                                                             \
    static struct behavior_sensor_iidx_x_value_data data_##n = {};                                 \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &data_##n, &cfg_##n, POST_KERNEL,                       \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);

DT_INST_FOREACH_STATUS_OKAY(INST)
