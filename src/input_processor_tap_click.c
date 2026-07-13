/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT roba_input_processor_tap_click

#include <zephyr/dt-bindings/input/input-event-codes.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <drivers/input_processor.h>

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/keymap.h>
#include <zmk/behavior.h>
#include <zmk/virtual_key_position.h>

struct tap_click_config {
    uint8_t index;
    uint16_t code;
    const struct zmk_behavior_binding *binding;
};

struct tap_click_data {
    bool touching;
};

static int tap_click_handle_event(const struct device *dev, struct input_event *event,
                                  uint32_t param1, uint32_t param2,
                                  struct zmk_input_processor_state *state) {
    const struct tap_click_config *cfg = dev->config;
    struct tap_click_data *data = dev->data;

    if (event->type != INPUT_EV_KEY || event->code != cfg->code) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    /* The pinnacle driver reports BTN_TOUCH state with EVERY relative packet,
     * not just on transitions; only invoke the behavior on an actual change so
     * motion packets don't spam press/release (and their HID reports). */
    bool pressed = event->value != 0;
    if (pressed != data->touching) {
        data->touching = pressed;

        struct zmk_behavior_binding_event behavior_event = {
            .position = ZMK_VIRTUAL_KEY_POSITION_BEHAVIOR_INPUT_PROCESSOR(
                state ? state->input_device_index : 0, cfg->index),
            .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
            .source = ZMK_POSITION_STATE_CHANGE_SOURCE_LOCAL,
#endif
        };

        int ret = zmk_behavior_invoke_binding(cfg->binding, behavior_event, pressed);
        if (ret < 0) {
            LOG_WRN("Failed to invoke tap-click binding: %d", ret);
        }
    }

    /* Neutralize the event but return CONTINUE (not STOP): the listener must
     * still see this event's sync flag to flush accumulated REL_X/Y, since
     * the pinnacle driver carries sync only on BTN_TOUCH. */
    event->code = INPUT_KEY_RESERVED;
    event->value = 0;
    return ZMK_INPUT_PROC_CONTINUE;
}

static struct zmk_input_processor_driver_api tap_click_driver_api = {
    .handle_event = tap_click_handle_event,
};

static int tap_click_init(const struct device *dev) { return 0; }

#define TAP_CLICK_INST(n)                                                                          \
    static const struct zmk_behavior_binding tap_click_binding_##n =                               \
        ZMK_KEYMAP_EXTRACT_BINDING(0, DT_DRV_INST(n));                                             \
    static struct tap_click_data tap_click_data_##n = {};                                          \
    static const struct tap_click_config tap_click_config_##n = {                                  \
        .index = n,                                                                                \
        .code = DT_INST_PROP(n, code),                                                             \
        .binding = &tap_click_binding_##n,                                                         \
    };                                                                                             \
    DEVICE_DT_INST_DEFINE(n, &tap_click_init, NULL, &tap_click_data_##n,                           \
                          &tap_click_config_##n, POST_KERNEL,                                      \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &tap_click_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TAP_CLICK_INST)
