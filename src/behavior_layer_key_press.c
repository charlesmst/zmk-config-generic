/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * &kp with two keycodes: param1 normally, param2 while a layer is active. One
 * Fn layer serves both machines this way -- the Windows base layer is on
 * exactly when DeskHop drives the Windows machine, so Fn+F3 can be Mission
 * Control on one and Task View on the other without a second Fn layer, which
 * on the Prospector dongle would cost a full layer of keymap RAM.
 *
 * The keycode chosen at press time is remembered per key position and released
 * as-is. Re-deciding at release would leave a key stuck down if DeskHop flipped
 * machines while it was held.
 */

#define DT_DRV_COMPAT roba_behavior_layer_key_press

#include <zephyr/device.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/events/keycode_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

/* Keys of this behavior held at once. Eight is a full hand on an Fn layer. */
#define HELD_MAX 8

struct behavior_layer_key_press_config {
    zmk_keymap_layer_id_t layer;
};

struct held_key {
    uint32_t position;
    uint32_t keycode;
    bool used;
};

struct behavior_layer_key_press_data {
    struct held_key held[HELD_MAX];
};

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_layer_key_press_config *config = dev->config;
    struct behavior_layer_key_press_data *data = dev->data;

    uint32_t keycode =
        zmk_keymap_layer_active(config->layer) ? binding->param2 : binding->param1;

    for (int i = 0; i < HELD_MAX; i++) {
        if (!data->held[i].used) {
            data->held[i] = (struct held_key){
                .position = event.position, .keycode = keycode, .used = true};
            break;
        }
    }

    return raise_zmk_keycode_state_changed_from_encoded(keycode, true, event.timestamp);
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_layer_key_press_config *config = dev->config;
    struct behavior_layer_key_press_data *data = dev->data;

    for (int i = 0; i < HELD_MAX; i++) {
        if (data->held[i].used && data->held[i].position == event.position) {
            data->held[i].used = false;
            return raise_zmk_keycode_state_changed_from_encoded(data->held[i].keycode, false,
                                                                event.timestamp);
        }
    }

    /* Only if more than HELD_MAX were down at once: release what the layer
     * picks now, which is right unless it also changed mid-press. */
    uint32_t keycode =
        zmk_keymap_layer_active(config->layer) ? binding->param2 : binding->param1;
    return raise_zmk_keycode_state_changed_from_encoded(keycode, false, event.timestamp);
}

static const struct behavior_driver_api behavior_layer_key_press_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

#define LAYER_KEY_PRESS_INST(n)                                                                    \
    static struct behavior_layer_key_press_data behavior_layer_key_press_data_##n = {};            \
    static const struct behavior_layer_key_press_config behavior_layer_key_press_config_##n = {    \
        .layer = DT_INST_PROP(n, layer),                                                           \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, NULL, NULL, &behavior_layer_key_press_data_##n,                     \
                            &behavior_layer_key_press_config_##n, POST_KERNEL,                     \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_layer_key_press_driver_api);

DT_INST_FOREACH_STATUS_OKAY(LAYER_KEY_PRESS_INST)

#endif
