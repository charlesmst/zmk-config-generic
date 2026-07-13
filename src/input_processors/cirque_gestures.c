/*
 * Copyright 2026 Charles Stein
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT roba_input_processor_cirque_gestures

#include <drivers/input_processor.h>
#include <zephyr/device.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <math.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define CIRQUE_PI 3.14159265358979323846f

struct cirque_gestures_config {
    uint16_t width;
    uint16_t height;
    uint8_t right_scroll_percent;
    uint16_t scroll_divisor;
    uint16_t circular_scroll_degrees_per_tick;
    uint16_t edge_scroll_activation_threshold;
    uint16_t edge_scroll_tangent_percent;
    uint16_t edge_scroll_recover_threshold;
    uint16_t edge_scroll_recover_radial_percent;
    uint16_t pointer_deadzone;
    uint16_t pointer_multiplier;
    uint16_t pointer_divisor;
    uint16_t touch_timeout_ms;
    bool invert_scroll;
    bool invert_y;
    bool tap_click;
    bool suppress_movement_during_tap;
    uint16_t tap_timeout_ms;
    uint16_t tap_move_threshold;
};

struct cirque_gestures_data {
    const struct device *dev;
    const struct device *input_dev;
    struct k_work_delayable touch_timeout_work;
    struct k_work tap_work;
    bool touching;
    bool edge_scroll;
    bool edge_scroll_pending;
    bool edge_pointer_emit_start_x;
    bool edge_pointer_emit_start_y;
    bool have_prev_x;
    bool have_prev_y;
    bool tap_candidate;
    bool have_start_x;
    bool have_start_y;
    int32_t prev_x;
    int32_t prev_y;
    int32_t start_x;
    int32_t start_y;
    bool have_scroll_angle;
    int32_t prev_scroll_angle;
    uint32_t touch_start_ms;
    int32_t scroll_remainder;
    int32_t pointer_x_remainder;
    int32_t pointer_y_remainder;
    int32_t sample_dx;
    bool have_sample_dx;
};

static int32_t iabs32(int32_t value) {
    return value < 0 ? -value : value;
}

static int64_t iabs64(int64_t value) {
    return value < 0 ? -value : value;
}

static void report_left_button(struct cirque_gestures_data *data, int32_t pressed) {
    if (data->input_dev == NULL) {
        return;
    }

    input_report(data->input_dev, INPUT_EV_KEY, INPUT_BTN_TOUCH, pressed, true, K_NO_WAIT);
}

static void emit_tap_work_callback(struct k_work *work) {
    struct cirque_gestures_data *data =
        CONTAINER_OF(work, struct cirque_gestures_data, tap_work);

    report_left_button(data, 1);
    report_left_button(data, 0);
}

static void maybe_emit_tap(const struct cirque_gestures_config *cfg,
                           struct cirque_gestures_data *data) {
    if (!cfg->tap_click || !data->tap_candidate || data->edge_scroll) {
        return;
    }

    uint32_t duration_ms = k_uptime_get_32() - data->touch_start_ms;
    if (duration_ms > cfg->tap_timeout_ms) {
        return;
    }

    k_work_submit(&data->tap_work);
}

static void end_touch(struct cirque_gestures_data *data) {
    data->touching = false;
    data->edge_scroll = false;
    data->edge_scroll_pending = false;
    data->edge_pointer_emit_start_x = false;
    data->edge_pointer_emit_start_y = false;
    data->have_prev_x = false;
    data->have_prev_y = false;
    data->tap_candidate = false;
    data->have_start_x = false;
    data->have_start_y = false;
    data->have_scroll_angle = false;
    data->scroll_remainder = 0;
    data->pointer_x_remainder = 0;
    data->pointer_y_remainder = 0;
    data->sample_dx = 0;
    data->have_sample_dx = false;
}

static void touch_timeout_callback(struct k_work *work) {
    struct k_work_delayable *dwork = k_work_delayable_from_work(work);
    struct cirque_gestures_data *data =
        CONTAINER_OF(dwork, struct cirque_gestures_data, touch_timeout_work);
    const struct cirque_gestures_config *cfg = data->dev->config;

    if (!data->touching) {
        return;
    }

    maybe_emit_tap(cfg, data);
    end_touch(data);
}

static void begin_touch(const struct cirque_gestures_config *cfg,
                        struct cirque_gestures_data *data, int32_t x) {
    const int32_t zone_width = ((int32_t)cfg->width * cfg->right_scroll_percent) / 100;
    const int32_t right_edge_start = cfg->width - zone_width;

    data->touching = true;
    data->edge_scroll = false;
    data->edge_scroll_pending = x >= right_edge_start;
    data->edge_pointer_emit_start_x = false;
    data->edge_pointer_emit_start_y = false;
    data->tap_candidate = true;
    data->touch_start_ms = k_uptime_get_32();
    data->have_scroll_angle = false;
    data->scroll_remainder = 0;
    data->pointer_x_remainder = 0;
    data->pointer_y_remainder = 0;
    data->sample_dx = 0;
    data->have_sample_dx = false;

    LOG_DBG("Cirque touch start x=%d edge_scroll_pending=%d", (int)x,
            data->edge_scroll_pending);
}

static int32_t scale_pointer_delta(const struct cirque_gestures_config *cfg, int32_t delta) {
    return (delta * cfg->pointer_multiplier) / cfg->pointer_divisor;
}

static int32_t apply_pointer_deadzone(const struct cirque_gestures_config *cfg,
                                      int32_t *remainder, int32_t delta) {
    if (cfg->pointer_deadzone == 0) {
        return delta;
    }

    *remainder += delta;

    if (iabs32(*remainder) <= cfg->pointer_deadzone) {
        return 0;
    }

    int32_t output = *remainder > 0 ? *remainder - cfg->pointer_deadzone
                                    : *remainder + cfg->pointer_deadzone;

    *remainder = *remainder > 0 ? cfg->pointer_deadzone : -cfg->pointer_deadzone;
    return output;
}

static void update_tap_candidate_from_motion(const struct cirque_gestures_config *cfg,
                                             struct cirque_gestures_data *data) {
    if (!data->tap_candidate || !data->have_start_x || !data->have_start_y) {
        return;
    }

    if (iabs32(data->prev_x - data->start_x) > cfg->tap_move_threshold ||
        iabs32(data->prev_y - data->start_y) > cfg->tap_move_threshold) {
        data->tap_candidate = false;
    }
}

static bool should_suppress_pointer_motion(const struct cirque_gestures_config *cfg,
                                           const struct cirque_gestures_data *data) {
    return cfg->suppress_movement_during_tap && data->tap_candidate && !data->edge_scroll;
}

static void resolve_edge_scroll_start(const struct cirque_gestures_config *cfg,
                                      struct cirque_gestures_data *data) {
    if (!data->edge_scroll_pending || !data->have_start_x || !data->have_start_y ||
        !data->have_prev_x || !data->have_prev_y) {
        return;
    }

    const int32_t dx = data->prev_x - data->start_x;
    const int32_t dy = data->prev_y - data->start_y;

    const int32_t max_delta = iabs32(dx) > iabs32(dy) ? iabs32(dx) : iabs32(dy);
    if (max_delta < cfg->edge_scroll_activation_threshold) {
        return;
    }

    const int32_t radial_x = data->start_x - ((int32_t)cfg->width / 2);
    const int32_t radial_y = data->start_y - ((int32_t)cfg->height / 2);
    const int64_t tangential = ((int64_t)dx * -radial_y) + ((int64_t)dy * radial_x);
    const int64_t radial = ((int64_t)dx * radial_x) + ((int64_t)dy * radial_y);
    const bool is_circular_start =
        iabs64(tangential) * 100 >= iabs64(radial) * cfg->edge_scroll_tangent_percent;

    data->edge_scroll_pending = false;
    data->tap_candidate = false;

    if (is_circular_start) {
        data->edge_scroll = true;
        data->have_scroll_angle = false;
        data->scroll_remainder = 0;
        LOG_DBG("Cirque edge gesture committed to scroll dx=%d dy=%d", (int)dx, (int)dy);
    } else {
        data->edge_pointer_emit_start_x = true;
        data->edge_pointer_emit_start_y = true;
        LOG_DBG("Cirque edge gesture committed to pointer dx=%d dy=%d", (int)dx, (int)dy);
    }
}

static bool recover_edge_scroll_to_pointer(const struct cirque_gestures_config *cfg,
                                           struct cirque_gestures_data *data, int32_t dx,
                                           int32_t dy) {
    if (!data->edge_scroll) {
        return false;
    }

    const int32_t max_delta = iabs32(dx) > iabs32(dy) ? iabs32(dx) : iabs32(dy);
    if (max_delta < cfg->edge_scroll_recover_threshold) {
        return false;
    }

    const int32_t radial_x = data->prev_x - ((int32_t)cfg->width / 2);
    const int32_t radial_y = data->prev_y - ((int32_t)cfg->height / 2);
    const int64_t tangential = ((int64_t)dx * -radial_y) + ((int64_t)dy * radial_x);
    const int64_t radial = ((int64_t)dx * radial_x) + ((int64_t)dy * radial_y);

    if (iabs64(radial) * 100 <
        iabs64(tangential) * cfg->edge_scroll_recover_radial_percent) {
        return false;
    }

    data->edge_scroll = false;
    data->have_scroll_angle = false;
    data->scroll_remainder = 0;
    data->pointer_x_remainder = 0;
    data->pointer_y_remainder = 0;

    LOG_DBG("Cirque edge gesture recovered to pointer dx=%d dy=%d", (int)dx, (int)dy);
    return true;
}

static int32_t current_angle_degrees(const struct cirque_gestures_config *cfg,
                                     const struct cirque_gestures_data *data) {
    const float x = (float)data->prev_x - ((float)cfg->width / 2.0f);
    const float y = (float)data->prev_y - ((float)cfg->height / 2.0f);
    float angle = atan2f(y, x) * (180.0f / CIRQUE_PI);

    if (angle < 0.0f) {
        angle += 360.0f;
    }

    return (int32_t)angle;
}

static int32_t normalize_angle_delta(int32_t delta) {
    while (delta > 180) {
        delta -= 360;
    }
    while (delta < -180) {
        delta += 360;
    }
    return delta;
}

static bool edge_scroll_ticks_from_angle(const struct cirque_gestures_config *cfg,
                                         struct cirque_gestures_data *data, int32_t *ticks) {
    if (!data->have_prev_x || !data->have_prev_y) {
        return false;
    }

    int32_t angle = current_angle_degrees(cfg, data);
    if (!data->have_scroll_angle) {
        data->prev_scroll_angle = angle;
        data->have_scroll_angle = true;
        return false;
    }

    int32_t delta = normalize_angle_delta(angle - data->prev_scroll_angle);
    data->prev_scroll_angle = angle;

    int32_t units = delta + data->scroll_remainder;
    *ticks = units / cfg->circular_scroll_degrees_per_tick;
    data->scroll_remainder = units - (*ticks * cfg->circular_scroll_degrees_per_tick);

    return *ticks != 0;
}

static bool edge_scroll_ticks_from_vertical(const struct cirque_gestures_config *cfg,
                                            struct cirque_gestures_data *data, int32_t delta,
                                            int32_t *ticks) {
    int32_t units = delta + data->scroll_remainder;

    *ticks = units / cfg->scroll_divisor;
    data->scroll_remainder = units - (*ticks * cfg->scroll_divisor);

    return *ticks != 0;
}

static int handle_abs_x(const struct cirque_gestures_config *cfg,
                        struct cirque_gestures_data *data, struct input_event *event) {
    const int32_t x = event->value;

    k_work_reschedule(&data->touch_timeout_work, K_MSEC(cfg->touch_timeout_ms));

    if (!data->touching) {
        begin_touch(cfg, data, x);
    }

    if (!data->have_prev_x) {
        data->prev_x = x;
        data->have_prev_x = true;
        data->start_x = x;
        data->have_start_x = true;
        return ZMK_INPUT_PROC_STOP;
    }

    const int32_t delta = x - data->prev_x;
    data->prev_x = x;
    data->sample_dx = delta;
    data->have_sample_dx = true;
    update_tap_candidate_from_motion(cfg, data);
    resolve_edge_scroll_start(cfg, data);

    if (data->edge_scroll || data->edge_scroll_pending ||
        should_suppress_pointer_motion(cfg, data)) {
        data->pointer_x_remainder = 0;
        return ZMK_INPUT_PROC_STOP;
    }

    int32_t pointer_delta = delta;
    if (data->edge_pointer_emit_start_x) {
        pointer_delta = data->prev_x - data->start_x;
        data->edge_pointer_emit_start_x = false;
    }

    const int32_t filtered_delta =
        apply_pointer_deadzone(cfg, &data->pointer_x_remainder, pointer_delta);

    event->type = INPUT_EV_REL;
    event->code = INPUT_REL_X;
    event->value = scale_pointer_delta(cfg, filtered_delta);
    event->sync = false;

    return event->value == 0 ? ZMK_INPUT_PROC_STOP : ZMK_INPUT_PROC_CONTINUE;
}

static int handle_abs_y(const struct cirque_gestures_config *cfg,
                        struct cirque_gestures_data *data, struct input_event *event) {
    const int32_t y = event->value;

    k_work_reschedule(&data->touch_timeout_work, K_MSEC(cfg->touch_timeout_ms));

    if (!data->touching) {
        begin_touch(cfg, data, data->have_prev_x ? data->prev_x : 0);
    }

    if (!data->have_prev_y) {
        data->prev_y = y;
        data->have_prev_y = true;
        data->start_y = y;
        data->have_start_y = true;
        return ZMK_INPUT_PROC_STOP;
    }

    const int32_t delta = y - data->prev_y;
    data->prev_y = y;
    const int32_t sample_dx = data->have_sample_dx ? data->sample_dx : 0;
    data->sample_dx = 0;
    data->have_sample_dx = false;
    update_tap_candidate_from_motion(cfg, data);
    resolve_edge_scroll_start(cfg, data);
    recover_edge_scroll_to_pointer(cfg, data, sample_dx, delta);

    if (data->edge_scroll) {
        int32_t ticks = 0;
        const bool had_scroll_angle = data->have_scroll_angle;
        bool have_ticks = edge_scroll_ticks_from_angle(cfg, data, &ticks);
        if (!have_ticks && !had_scroll_angle) {
            have_ticks = edge_scroll_ticks_from_vertical(cfg, data, delta, &ticks);
        }

        if (!have_ticks) {
            return ZMK_INPUT_PROC_STOP;
        }

        if (cfg->invert_scroll) {
            ticks = -ticks;
        }

        event->type = INPUT_EV_REL;
        event->code = INPUT_REL_WHEEL;
        event->value = ticks;
        event->sync = true;

        return ZMK_INPUT_PROC_CONTINUE;
    }

    if (data->edge_scroll_pending || should_suppress_pointer_motion(cfg, data)) {
        data->pointer_y_remainder = 0;
        return ZMK_INPUT_PROC_STOP;
    }

    int32_t pointer_delta = delta;
    if (data->edge_pointer_emit_start_y) {
        pointer_delta = data->prev_y - data->start_y;
        data->edge_pointer_emit_start_y = false;
    }

    const int32_t filtered_delta =
        apply_pointer_deadzone(cfg, &data->pointer_y_remainder, pointer_delta);

    event->type = INPUT_EV_REL;
    event->code = INPUT_REL_Y;
    event->value = scale_pointer_delta(cfg, cfg->invert_y ? -filtered_delta : filtered_delta);
    event->sync = true;

    return ZMK_INPUT_PROC_CONTINUE;
}

static int cirque_gestures_handle_event(const struct device *dev, struct input_event *event,
                                        uint32_t param1, uint32_t param2,
                                        struct zmk_input_processor_state *state) {
    const struct cirque_gestures_config *cfg = dev->config;
    struct cirque_gestures_data *data = dev->data;

    ARG_UNUSED(param1);
    ARG_UNUSED(param2);
    ARG_UNUSED(state);

    data->input_dev = event->dev;

    if (event->type != INPUT_EV_ABS) {
        return ZMK_INPUT_PROC_CONTINUE;
    }

    switch (event->code) {
    case INPUT_ABS_X:
        return handle_abs_x(cfg, data, event);
    case INPUT_ABS_Y:
        return handle_abs_y(cfg, data, event);
    case INPUT_ABS_Z:
        if (event->value == 0) {
            k_work_cancel_delayable(&data->touch_timeout_work);
            maybe_emit_tap(cfg, data);
            end_touch(data);
        }
        return ZMK_INPUT_PROC_STOP;
    default:
        return ZMK_INPUT_PROC_CONTINUE;
    }
}

static int cirque_gestures_init(const struct device *dev) {
    struct cirque_gestures_data *data = dev->data;

    data->dev = dev;
    k_work_init_delayable(&data->touch_timeout_work, touch_timeout_callback);
    k_work_init(&data->tap_work, emit_tap_work_callback);

    return 0;
}

static const struct zmk_input_processor_driver_api cirque_gestures_api = {
    .handle_event = cirque_gestures_handle_event,
};

#define CIRQUE_GESTURES_INST(n)                                                                    \
    BUILD_ASSERT(DT_INST_PROP(n, scroll_divisor) > 0, "scroll-divisor must be greater than zero"); \
    BUILD_ASSERT(DT_INST_PROP(n, pointer_divisor) > 0,                                             \
                 "pointer-divisor must be greater than zero");                                    \
    BUILD_ASSERT(DT_INST_PROP(n, edge_scroll_tangent_percent) > 0,                                 \
                 "edge-scroll-tangent-percent must be greater than zero");                        \
    static const struct cirque_gestures_config cirque_gestures_config_##n = {                      \
        .width = DT_INST_PROP(n, width),                                                           \
        .height = DT_INST_PROP(n, height),                                                         \
        .right_scroll_percent = DT_INST_PROP(n, right_scroll_percent),                             \
        .scroll_divisor = DT_INST_PROP(n, scroll_divisor),                                         \
        .circular_scroll_degrees_per_tick = DT_INST_PROP(n, circular_scroll_degrees_per_tick),     \
        .edge_scroll_activation_threshold = DT_INST_PROP(n, edge_scroll_activation_threshold),     \
        .edge_scroll_tangent_percent = DT_INST_PROP(n, edge_scroll_tangent_percent),               \
        .edge_scroll_recover_threshold = DT_INST_PROP(n, edge_scroll_recover_threshold),           \
        .edge_scroll_recover_radial_percent =                                                      \
            DT_INST_PROP(n, edge_scroll_recover_radial_percent),                                   \
        .pointer_deadzone = DT_INST_PROP(n, pointer_deadzone),                                     \
        .pointer_multiplier = DT_INST_PROP(n, pointer_multiplier),                                 \
        .pointer_divisor = DT_INST_PROP(n, pointer_divisor),                                       \
        .touch_timeout_ms = DT_INST_PROP(n, touch_timeout_ms),                                     \
        .invert_scroll = DT_INST_PROP(n, invert_scroll),                                           \
        .invert_y = DT_INST_PROP(n, invert_y),                                                     \
        .tap_click = DT_INST_PROP(n, tap_click),                                                   \
        .suppress_movement_during_tap = DT_INST_PROP(n, suppress_movement_during_tap),             \
        .tap_timeout_ms = DT_INST_PROP(n, tap_timeout_ms),                                         \
        .tap_move_threshold = DT_INST_PROP(n, tap_move_threshold),                                 \
    };                                                                                             \
    static struct cirque_gestures_data cirque_gestures_data_##n;                                   \
    DEVICE_DT_INST_DEFINE(n, cirque_gestures_init, NULL, &cirque_gestures_data_##n,                \
                          &cirque_gestures_config_##n, POST_KERNEL,                               \
                          CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &cirque_gestures_api);

DT_INST_FOREACH_STATUS_OKAY(CIRQUE_GESTURES_INST)
