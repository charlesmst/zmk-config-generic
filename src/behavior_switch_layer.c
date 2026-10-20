/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * A slide switch read as a held key that keeps a layer on while it is on and
 * another layer is active -- the Keychron's Mac/Win switch keeping Gaming on
 * over Windows.
 *
 * The switch state lives here, not in the layer stack. A conditional layer
 * would need a layer held for the switch, and anything that rewrites the stack
 * drops it: &to deactivates every layer, so after one the switch would have to
 * be flipped again before it counted. Here the stack can be rewritten any way
 * at all: every layer change schedules a re-check, and the re-check puts
 * then-layer back when the switch is on and if-layer is active. That is what
 * brings Gaming back when DeskHop returns to the Windows machine, since
 * DeskHop's switch clears every base layer, Gaming included, on the way out.
 *
 * The re-check runs from a work item rather than inside the layer listener:
 * activating a layer raises another layer event, and DeskHop deactivates and
 * activates in a loop, so acting mid-loop could be undone by its next step.
 */

#define DT_DRV_COMPAT roba_behavior_switch_layer

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <drivers/behavior.h>
#include <zmk/behavior.h>
#include <zmk/event_manager.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/keymap.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_switch_layer_config {
    zmk_keymap_layer_id_t if_layer;
    zmk_keymap_layer_id_t then_layer;
};

struct behavior_switch_layer_data {
    const struct device *dev;
    struct k_work work;
    /* The switch is on: its key position is held. */
    bool switch_on;
    /* then-layer is on because of the switch, so turning the switch off
     * turns it off. Cleared when if-layer goes away with the switch still on,
     * which hands then-layer back to whatever changed the layers. */
    bool owned;
};

static void switch_layer_sync(const struct device *dev) {
    const struct behavior_switch_layer_config *config = dev->config;
    struct behavior_switch_layer_data *data = dev->data;

    if (data->switch_on && zmk_keymap_layer_active(config->if_layer)) {
        if (!zmk_keymap_layer_active(config->then_layer)) {
            LOG_DBG("switch layer: %d on over %d", config->then_layer, config->if_layer);
            /* Locking, as &tog and &to activate, so a &tog of the same layer
             * sees it as toggled on rather than as momentary. */
            zmk_keymap_layer_activate(config->then_layer, true);
        }
        data->owned = true;
        return;
    }

    if (!data->switch_on && data->owned && zmk_keymap_layer_active(config->then_layer)) {
        LOG_DBG("switch layer: %d off, switch off", config->then_layer);
        /* Forcing: the activate above set the lock bit. */
        zmk_keymap_layer_deactivate(config->then_layer, true);
    }
    data->owned = false;
}

static void switch_layer_work_cb(struct k_work *work) {
    struct behavior_switch_layer_data *data =
        CONTAINER_OF(work, struct behavior_switch_layer_data, work);

    switch_layer_sync(data->dev);
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_switch_layer_data *data = dev->data;

    data->switch_on = true;
    k_work_submit(&data->work);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_switch_layer_data *data = dev->data;

    data->switch_on = false;
    k_work_submit(&data->work);

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_switch_layer_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
};

#define GET_DEV(inst) DEVICE_DT_INST_GET(inst),
static const struct device *devs[] = {DT_INST_FOREACH_STATUS_OKAY(GET_DEV)};

static int switch_layer_layer_state_changed_listener(const zmk_event_t *eh) {
    if (as_zmk_layer_state_changed(eh) == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < ARRAY_SIZE(devs); i++) {
        struct behavior_switch_layer_data *data = devs[i]->data;

        if (data->switch_on || data->owned) {
            k_work_submit(&data->work);
        }
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_switch_layer, switch_layer_layer_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_switch_layer, zmk_layer_state_changed);

static int behavior_switch_layer_init(const struct device *dev) {
    struct behavior_switch_layer_data *data = dev->data;

    data->dev = dev;
    k_work_init(&data->work, switch_layer_work_cb);

    return 0;
}

#define SWITCH_LAYER_INST(n)                                                                       \
    static struct behavior_switch_layer_data behavior_switch_layer_data_##n = {};                  \
    static const struct behavior_switch_layer_config behavior_switch_layer_config_##n = {          \
        .if_layer = DT_INST_PROP(n, if_layer),                                                     \
        .then_layer = DT_INST_PROP(n, then_layer),                                                 \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_switch_layer_init, NULL, &behavior_switch_layer_data_##n,  \
                            &behavior_switch_layer_config_##n, POST_KERNEL,                        \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_switch_layer_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SWITCH_LAYER_INST)

#endif
