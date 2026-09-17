/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * Latching auto-clicker. Press once and the configured HID button is clicked
 * on a timer until either the same key is pressed again or any other key
 * position goes down -- the caps-word "stays on until you type something
 * else" shape, applied to a mouse button.
 *
 * The clicks come from a delayable work item rather than a macro: ZMK macros
 * run a fixed number of steps and occupy the behavior queue while they do, so
 * a long burst of clicks would stall every other key.
 */

#define DT_DRV_COMPAT roba_behavior_autoclick

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <drivers/behavior.h>
#include <dt-bindings/zmk/pointing.h>
#include <zmk/behavior.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/hid.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_autoclick_config {
    uint32_t press_ms;
    uint32_t interval_ms;
};

struct behavior_autoclick_data {
    const struct device *dev;
    struct k_work_delayable work;
    /* Clicking right now. Written from the ZMK thread, read from the work
     * item, so the work item re-checks it after every press. */
    bool active;
    /* This driver is currently holding the button down. */
    bool button_down;
    /* Button mask being clicked, taken from the binding parameter. */
    uint32_t buttons;
    /* Key position that started the run, so its own release (and its second
     * press, which toggles off through the binding) does not stop it. */
    uint32_t position;
};

static void autoclick_report(uint32_t buttons, bool press) {
    for (uint16_t bit = 0; bit < ZMK_HID_MOUSE_NUM_BUTTONS; bit++) {
        if ((buttons & BIT(bit)) == 0) {
            continue;
        }
        if (press) {
            zmk_hid_mouse_button_press(bit);
        } else {
            /* ZMK clamps the per-button press count at zero, so releasing a
             * button we are not holding is harmless. */
            zmk_hid_mouse_button_release(bit);
        }
    }
    zmk_endpoint_send_mouse_report();
}

static void autoclick_stop(const struct device *dev) {
    struct behavior_autoclick_data *data = dev->data;

    if (!data->active) {
        return;
    }

    LOG_DBG("autoclick stop, buttons 0x%02X", data->buttons);

    data->active = false;
    k_work_cancel_delayable(&data->work);

    /* Release unconditionally rather than only when button_down is set: the
     * cancel above does not wait for an in-flight work item, and an extra
     * release is clamped away. */
    data->button_down = false;
    autoclick_report(data->buttons, false);
}

static void autoclick_work_cb(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct behavior_autoclick_data *data = CONTAINER_OF(dwork, struct behavior_autoclick_data, work);
    const struct behavior_autoclick_config *config = data->dev->config;

    if (!data->active) {
        return;
    }

    if (data->button_down) {
        data->button_down = false;
        autoclick_report(data->buttons, false);
        k_work_schedule(&data->work, K_MSEC(config->interval_ms));
        return;
    }

    data->button_down = true;
    autoclick_report(data->buttons, true);

    /* Re-check after pressing: autoclick_stop() may have run between the
     * check at the top and the press, and its release would then have landed
     * before this press. Undo it here instead of leaving the button stuck. */
    if (!data->active) {
        data->button_down = false;
        autoclick_report(data->buttons, false);
        return;
    }

    k_work_schedule(&data->work, K_MSEC(config->press_ms));
}

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    struct behavior_autoclick_data *data = dev->data;

    if (data->active) {
        autoclick_stop(dev);
        return ZMK_BEHAVIOR_OPAQUE;
    }

    data->buttons = binding->param1;
    data->position = event.position;
    data->active = true;
    data->button_down = false;

    LOG_DBG("autoclick start at position %d, buttons 0x%02X", event.position, data->buttons);

    k_work_schedule(&data->work, K_NO_WAIT);

    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    /* Latching: the run outlives the key release. */
    ARG_UNUSED(binding);
    ARG_UNUSED(event);
    return ZMK_BEHAVIOR_OPAQUE;
}

#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
static const struct behavior_parameter_value_metadata button_values[] = {
    {.display_name = "MB1", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = MB1},
    {.display_name = "MB2", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = MB2},
    {.display_name = "MB3", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = MB3},
    {.display_name = "MB4", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = MB4},
    {.display_name = "MB5", .type = BEHAVIOR_PARAMETER_VALUE_TYPE_VALUE, .value = MB5},
};

static const struct behavior_parameter_metadata_set button_metadata_set[] = {{
    .param1_values = button_values,
    .param1_values_len = ARRAY_SIZE(button_values),
}};

static const struct behavior_parameter_metadata button_metadata = {
    .sets_len = ARRAY_SIZE(button_metadata_set),
    .sets = button_metadata_set,
};
#endif

static const struct behavior_driver_api behavior_autoclick_driver_api = {
    .binding_pressed = on_keymap_binding_pressed,
    .binding_released = on_keymap_binding_released,
#if IS_ENABLED(CONFIG_ZMK_BEHAVIOR_METADATA)
    .parameter_metadata = &button_metadata,
#endif
};

#define GET_DEV(inst) DEVICE_DT_INST_GET(inst),
static const struct device *devs[] = {DT_INST_FOREACH_STATUS_OKAY(GET_DEV)};

static int autoclick_position_state_changed_listener(const zmk_event_t *eh) {
    struct zmk_position_state_changed *ev = as_zmk_position_state_changed(eh);
    if (ev == NULL || !ev->state) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    for (int i = 0; i < ARRAY_SIZE(devs); i++) {
        const struct device *dev = devs[i];
        struct behavior_autoclick_data *data = dev->data;

        if (!data->active || ev->position == data->position) {
            continue;
        }

        LOG_DBG("autoclick stopped by position %d", ev->position);
        autoclick_stop(dev);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(behavior_autoclick, autoclick_position_state_changed_listener);
ZMK_SUBSCRIPTION(behavior_autoclick, zmk_position_state_changed);

static int behavior_autoclick_init(const struct device *dev) {
    struct behavior_autoclick_data *data = dev->data;

    data->dev = dev;
    k_work_init_delayable(&data->work, autoclick_work_cb);

    return 0;
}

#define AUTOCLICK_INST(n)                                                                          \
    static struct behavior_autoclick_data behavior_autoclick_data_##n = {};                        \
    static const struct behavior_autoclick_config behavior_autoclick_config_##n = {                \
        .press_ms = DT_INST_PROP(n, press_ms),                                                     \
        .interval_ms = DT_INST_PROP(n, interval_ms),                                               \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_autoclick_init, NULL, &behavior_autoclick_data_##n,        \
                            &behavior_autoclick_config_##n, POST_KERNEL,                           \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_autoclick_driver_api);

DT_INST_FOREACH_STATUS_OKAY(AUTOCLICK_INST)

#endif
