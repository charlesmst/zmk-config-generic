#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zmk/event_manager.h>
#include <zmk/events/battery_state_changed.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* This runs on a split peripheral (e.g. the XIAO BLE keyboard halves) and
 * blinks the board's onboard red LED when *this* device's own battery is low.
 * It mirrors the low-battery indication lariska shows, but as a plain GPIO
 * blink since the XIAO's onboard RGB LED is a gpio-leds node, not PWM.
 *
 * The XIAO BLE exposes its onboard LED via the led0 (red) / led1 (green) /
 * led2 (blue) aliases, all active-low. We drive led0 through gpio_dt_spec, so
 * the ACTIVE_LOW polarity is handled for us (set_dt(1) == lit). */
#define LED_NODE DT_ALIAS(led0)
BUILD_ASSERT(DT_NODE_EXISTS(LED_NODE), "led0 alias not found for self battery LED");

static const struct gpio_dt_spec low_batt_led = GPIO_DT_SPEC_GET(LED_NODE, gpios);

#define LOW_BATTERY_THRESHOLD CONFIG_ROBA_SELF_BATTERY_LED_THRESHOLD
/* Clear only after the battery rises 5% above the threshold to avoid bouncing
 * on and off around the edge. */
#define CLEAR_THRESHOLD      (LOW_BATTERY_THRESHOLD + 5)
#define BLINK_HALF_PERIOD_MS 200
#define BLINK_COUNT          3
#define REPEAT_INTERVAL_S    5
#define BOOT_FLASH_MS        250
#define BOOT_FLASH_TOGGLES   4 /* 4 toggles => 2 visible flashes at startup */

static struct k_work_delayable blink_work;
static struct k_work_delayable repeat_work;
static struct k_work_delayable boot_work;
static int blinks_remaining;
static int boot_toggles_remaining;
static bool battery_low;

static void blink_work_cb(struct k_work *work) {
    if (blinks_remaining <= 0) {
        gpio_pin_set_dt(&low_batt_led, 0);
        return;
    }
    gpio_pin_toggle_dt(&low_batt_led);
    blinks_remaining--;
    k_work_reschedule(&blink_work, K_MSEC(BLINK_HALF_PERIOD_MS));
}

static void repeat_work_cb(struct k_work *work) {
    if (!battery_low) {
        gpio_pin_set_dt(&low_batt_led, 0);
        return;
    }
    /* Start each burst from a known-off state, then re-arm the next burst. */
    gpio_pin_set_dt(&low_batt_led, 0);
    blinks_remaining = BLINK_COUNT * 2;
    k_work_reschedule(&blink_work, K_NO_WAIT);
    k_work_reschedule(&repeat_work, K_SECONDS(REPEAT_INTERVAL_S));
}

/* Startup self-test: flash a couple of times so the LED wiring is confirmed
 * immediately, without waiting for the first battery report (~up to a minute). */
static void boot_work_cb(struct k_work *work) {
    if (boot_toggles_remaining <= 0) {
        gpio_pin_set_dt(&low_batt_led, 0);
        return;
    }
    gpio_pin_toggle_dt(&low_batt_led);
    boot_toggles_remaining--;
    k_work_reschedule(&boot_work, K_MSEC(BOOT_FLASH_MS));
}

static int self_battery_led_listener(const zmk_event_t *eh) {
    const struct zmk_battery_state_changed *ev = as_zmk_battery_state_changed(eh);
    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    bool was_low = battery_low;
    if (ev->state_of_charge > 0 && ev->state_of_charge <= LOW_BATTERY_THRESHOLD) {
        battery_low = true;
    } else if (ev->state_of_charge > CLEAR_THRESHOLD) {
        battery_low = false;
    }
    LOG_WRN("Self battery report: %d%% (low=%d, threshold=%d)", ev->state_of_charge,
            battery_low, LOW_BATTERY_THRESHOLD);

    if (battery_low && !was_low) {
        k_work_reschedule(&repeat_work, K_NO_WAIT);
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(self_battery_led, self_battery_led_listener);
ZMK_SUBSCRIPTION(self_battery_led, zmk_battery_state_changed);

static int self_battery_led_init(void) {
    if (!gpio_is_ready_dt(&low_batt_led)) {
        LOG_ERR("Self battery LED GPIO not ready");
        return -ENODEV;
    }
    gpio_pin_configure_dt(&low_batt_led, GPIO_OUTPUT_INACTIVE);
    k_work_init_delayable(&blink_work, blink_work_cb);
    k_work_init_delayable(&repeat_work, repeat_work_cb);
    k_work_init_delayable(&boot_work, boot_work_cb);
    boot_toggles_remaining = BOOT_FLASH_TOGGLES;
    k_work_schedule(&boot_work, K_MSEC(500));
    return 0;
}

SYS_INIT(self_battery_led_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
