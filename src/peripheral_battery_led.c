#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define LED_RED_NODE   DT_ALIAS(led_red)
#define LED_GREEN_NODE DT_ALIAS(led_green)
#define LED_BLUE_NODE  DT_ALIAS(led_blue)
BUILD_ASSERT(DT_NODE_EXISTS(LED_RED_NODE),   "led-red alias not found");
BUILD_ASSERT(DT_NODE_EXISTS(LED_GREEN_NODE), "led-green alias not found");
BUILD_ASSERT(DT_NODE_EXISTS(LED_BLUE_NODE),  "led-blue alias not found");

static const struct gpio_dt_spec led_red   = GPIO_DT_SPEC_GET(LED_RED_NODE,   gpios);
static const struct gpio_dt_spec led_green = GPIO_DT_SPEC_GET(LED_GREEN_NODE, gpios);
static const struct gpio_dt_spec led_blue  = GPIO_DT_SPEC_GET(LED_BLUE_NODE,  gpios);

/* peripheral 0 → red, peripheral 1 → blue */
static const struct gpio_dt_spec *peripheral_led[] = { &led_red, &led_blue };
BUILD_ASSERT(ARRAY_SIZE(peripheral_led) == 2, "update peripheral_led for your peripheral count");

#define LOW_BATTERY_THRESHOLD CONFIG_ROBA_PERIPHERAL_BATTERY_LED_THRESHOLD
/* Clear only when battery rises 5% above the threshold to avoid bouncing */
#define CLEAR_THRESHOLD       (LOW_BATTERY_THRESHOLD + 5)
#define BLINK_HALF_PERIOD_MS  200
#define BLINK_COUNT           3
#define REPEAT_INTERVAL_S     5

static struct k_work_delayable blink_work;
static struct k_work_delayable repeat_work;
static struct k_work_delayable boot_green_off_work;
static int blinks_remaining;
/* Per-peripheral low state; index = ev->source */
static bool peripheral_low[CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS];

static bool any_low(void) {
    for (int i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
        if (peripheral_low[i]) {
            return true;
        }
    }
    return false;
}

static void boot_green_off_cb(struct k_work *work) {
    gpio_pin_set_dt(&led_green, 0);
}

static void blink_work_cb(struct k_work *work) {
    if (blinks_remaining <= 0) {
        for (int i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
            gpio_pin_set_dt(peripheral_led[i], 0);
        }
        return;
    }
    for (int i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
        if (peripheral_low[i]) {
            gpio_pin_toggle_dt(peripheral_led[i]);
        }
    }
    blinks_remaining--;
    k_work_reschedule(&blink_work, K_MSEC(BLINK_HALF_PERIOD_MS));
}

static void repeat_work_cb(struct k_work *work) {
    if (!any_low()) {
        LOG_WRN("Battery OK, stopping blink");
        for (int i = 0; i < CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS; i++) {
            gpio_pin_set_dt(peripheral_led[i], 0);
        }
        return;
    }
    LOG_WRN("Battery still low, blinking");
    blinks_remaining = BLINK_COUNT * 2;
    k_work_reschedule(&blink_work, K_NO_WAIT);
    k_work_reschedule(&repeat_work, K_SECONDS(REPEAT_INTERVAL_S));
}

static int peripheral_battery_led_listener(const zmk_event_t *eh) {
    const struct zmk_peripheral_battery_state_changed *ev =
        as_zmk_peripheral_battery_state_changed(eh);
    if (ev == NULL || ev->source >= CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool was_any_low = any_low();

    LOG_WRN("Peripheral %d battery report: %d%% (low=%d)", ev->source, ev->state_of_charge, peripheral_low[ev->source]);
    if (ev->state_of_charge > 0 && ev->state_of_charge <= LOW_BATTERY_THRESHOLD) {
        peripheral_low[ev->source] = true;
    } else if (ev->state_of_charge > CLEAR_THRESHOLD) {
        peripheral_low[ev->source] = false;
    }

    bool is_any_low = any_low();

    if (is_any_low && !was_any_low) {
        k_work_reschedule(&repeat_work, K_NO_WAIT);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(peripheral_battery_led, peripheral_battery_led_listener);
ZMK_SUBSCRIPTION(peripheral_battery_led, zmk_peripheral_battery_state_changed);

static int peripheral_battery_led_init(void) {
    if (!gpio_is_ready_dt(&led_red) || !gpio_is_ready_dt(&led_green) || !gpio_is_ready_dt(&led_blue)) {
        LOG_ERR("LED GPIO device not ready");
        return -ENODEV;
    }
    gpio_pin_configure_dt(&led_red,   GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_green, GPIO_OUTPUT_INACTIVE);
    gpio_pin_configure_dt(&led_blue,  GPIO_OUTPUT_INACTIVE);
    k_work_init_delayable(&blink_work, blink_work_cb);
    k_work_init_delayable(&repeat_work, repeat_work_cb);
    k_work_init_delayable(&boot_green_off_work, boot_green_off_cb);
    gpio_pin_set_dt(&led_green, 1);
    k_work_schedule(&boot_green_off_work, K_SECONDS(2));
    return 0;
}

SYS_INIT(peripheral_battery_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
