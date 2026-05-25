/*
 * Copyright (c) 2026 Charles Stein
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT roba_input_processor_ble_report_rate_limit

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <drivers/input_processor.h>

#if IS_ENABLED(CONFIG_ZMK_BLE)
#include <zmk/endpoints.h>
#include <zmk/endpoints_types.h>
#endif

#define ROBA_BLE_RRL_MAX_CODES 4

struct roba_ble_rrl_config {
    uint8_t type;
    size_t codes_len;
    uint16_t codes[ROBA_BLE_RRL_MAX_CODES];
};

struct roba_ble_rrl_data {
    int32_t remainders[ROBA_BLE_RRL_MAX_CODES];
    bool syncs[ROBA_BLE_RRL_MAX_CODES];
    int64_t last_report_ms[ROBA_BLE_RRL_MAX_CODES];
};

static bool endpoint_is_ble(void) {
#if IS_ENABLED(CONFIG_ZMK_BLE)
    return zmk_endpoints_selected().transport == ZMK_TRANSPORT_BLE;
#else
    return false;
#endif
}

static int limit_code(const struct device *dev, struct input_event *event, int code_idx,
                      uint32_t delay_ms) {
    struct roba_ble_rrl_data *data = dev->data;
    int64_t now = k_uptime_get();

    if (now - data->last_report_ms[code_idx] >= delay_ms * ROBA_BLE_RRL_MAX_CODES) {
        data->remainders[code_idx] = 0;
        data->syncs[code_idx] = false;
    }

    if (now - data->last_report_ms[code_idx] < delay_ms) {
        data->remainders[code_idx] =
            CLAMP(data->remainders[code_idx] + event->value, INT16_MIN, INT16_MAX);
        data->syncs[code_idx] |= event->sync;
        event->value = 0;
        event->sync = false;
        return ZMK_INPUT_PROC_STOP;
    }

    event->value = CLAMP(event->value + data->remainders[code_idx], INT16_MIN, INT16_MAX);
    event->sync |= data->syncs[code_idx];
    data->remainders[code_idx] = 0;
    data->syncs[code_idx] = false;
    data->last_report_ms[code_idx] = now;

    return ZMK_INPUT_PROC_CONTINUE;
}

static int handle_event(const struct device *dev, struct input_event *event, uint32_t param1,
                        uint32_t param2, struct zmk_input_processor_state *state) {
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    if (!endpoint_is_ble()) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    const struct roba_ble_rrl_config *cfg = dev->config;
    if (event->type != cfg->type) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    for (int i = 0; i < cfg->codes_len; i++) {
        if (cfg->codes[i] == event->code) {
            return limit_code(dev, event, i, param1);
        }
    }

    return ZMK_INPUT_PROC_CONTINUE;
}

static int init(const struct device *dev) {
    struct roba_ble_rrl_data *data = dev->data;
    int64_t now = k_uptime_get();

    for (int i = 0; i < ROBA_BLE_RRL_MAX_CODES; i++) {
        data->last_report_ms[i] = now;
    }

    return 0;
}

static const struct zmk_input_processor_driver_api api = {
    .handle_event = handle_event,
};

#define DEFINE_ROBA_BLE_RRL(n)                                                                 \
    BUILD_ASSERT(DT_INST_PROP_LEN(n, codes) <= ROBA_BLE_RRL_MAX_CODES,                         \
                 "Too many BLE report rate limit input codes");                                \
    static struct roba_ble_rrl_data data_##n = {};                                              \
    static const struct roba_ble_rrl_config config_##n = {                                      \
        .type = DT_INST_PROP(n, type),                                                         \
        .codes_len = DT_INST_PROP_LEN(n, codes),                                               \
        .codes = DT_INST_PROP(n, codes),                                                       \
    };                                                                                         \
    DEVICE_DT_INST_DEFINE(n, init, NULL, &data_##n, &config_##n, POST_KERNEL,                   \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &api);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_ROBA_BLE_RRL)
