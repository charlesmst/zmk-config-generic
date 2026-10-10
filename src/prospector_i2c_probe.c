/*
 * One-shot diagnostic: what is actually on the Prospector's I2C bus?
 *
 * The Prospector adapter devicetree wires the ST7789 over spi3 (write-only),
 * the APDS9960 light sensor on i2c1, and a PWM backlight. There is no touch
 * controller node anywhere in the upstream module, yet the hardware ships a
 * Waveshare 27057 panel that *has* a digitizer -- upstream even requires the
 * touch variant, because the non-touch one does not fit the case.
 *
 * So the open question is whether the PCB routes the panel's TP_SDA / TP_SCL to
 * the same i2c1 the light sensor sits on. If it does, a capacitive controller
 * answers at a known address and the touch UI is buildable with no rewiring.
 * If nothing answers, input has to come from the APDS9960's gesture engine
 * instead (the module only ever reads SENSOR_CHAN_LIGHT from it today).
 *
 * This scans the bus and logs every responder. It needs a log backend, so
 * build it with roBakesb_prospector_probe.conf -- see
 * docs/prospector-dongle.md "Is there a touch panel?".
 *
 * Diagnostic only. Not enabled in any build.yaml target.
 */

#include <stdio.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#if IS_ENABLED(CONFIG_ZMK_DISPLAY)
#include <lvgl.h>
#include <zmk/display.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/*
 * The result also goes on the screen, not just the log.
 *
 * This build carries three CDC instances (zmk-usb-logging, studio-rpc and the
 * board console) and the ports enumerate unreliably -- they appear, then vanish
 * before anything can read them. The display, meanwhile, is the one channel on
 * this device that demonstrably works. So the scan paints its own answer over
 * the status screen as an LVGL overlay, which needs no fork change: the overlay
 * is added to the active screen at runtime rather than replacing it.
 */
#if IS_ENABLED(CONFIG_ZMK_DISPLAY)
static char result_text[96] = "i2c: scanning";
static lv_obj_t *result_label;

static void result_draw_fn(struct k_work *work) {
    ARG_UNUSED(work);

    if (result_label == NULL) {
        lv_obj_t *screen = lv_screen_active();
        if (screen == NULL) {
            return;
        }
        result_label = lv_label_create(screen);
        lv_obj_set_style_bg_color(result_label, lv_color_black(), LV_PART_MAIN);
        lv_obj_set_style_bg_opa(result_label, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_text_color(result_label, lv_color_white(), LV_PART_MAIN);
        lv_obj_set_style_pad_all(result_label, 4, LV_PART_MAIN);
        lv_obj_align(result_label, LV_ALIGN_CENTER, 0, 0);
    }

    lv_label_set_text(result_label, result_text);
}

static K_WORK_DEFINE(result_draw, result_draw_fn);

static void publish_result(const char *text) {
    strncpy(result_text, text, sizeof(result_text) - 1);
    result_text[sizeof(result_text) - 1] = '\0';
    if (zmk_display_is_initialized()) {
        k_work_submit_to_queue(zmk_display_work_q(), &result_draw);
    }
}
#else
static void publish_result(const char *text) { ARG_UNUSED(text); }
#endif

/* Controllers worth recognising by sight. The first is the one we are hoping
 * for: CST816S is what Waveshare fits to the 1.69" round panel. */
static const char *known_device(uint16_t addr) {
    switch (addr) {
    case 0x15:
        return " <- CST816S touch (Waveshare 1.69\" panel)";
    case 0x38:
        return " <- FT6236/FT6336 touch";
    case 0x14:
    case 0x5D:
        return " <- GT911 touch";
    case 0x39:
        return " <- APDS9960 light/gesture (expected)";
    default:
        return "";
    }
}

/*
 * Independent of the scan, and the tie-breaker when the scan comes back empty.
 *
 * The APDS9960 driver talks to the chip during its own init -- it cannot bind
 * unless the device acknowledged on the bus. So device_is_ready() here is a
 * second, side-effect-free opinion on whether anything is really out there:
 *
 *   scan empty + driver not ready -> nothing on the bus. The sensor is the
 *       optional part of the Prospector BOM, so "not fitted" explains it, and
 *       a missing digitizer explains the rest.
 *   scan empty + driver ready     -> the chip is present and answered the
 *       driver but not us. Then this probe is wrong, not the hardware, and
 *       nothing can be concluded about touch yet.
 */
static const char *sensor_state(void) {
#if DT_HAS_COMPAT_STATUS_OKAY(avago_apds9960)
    const struct device *sensor = DEVICE_DT_GET_ONE(avago_apds9960);
    return device_is_ready(sensor) ? "APDS RDY" : "APDS N/R";
#else
    return "APDS ABSENT-DT";
#endif
}

static void probe_once(void) {
    const struct device *bus = DEVICE_DT_GET(DT_NODELABEL(i2c1));

    if (!device_is_ready(bus)) {
        LOG_ERR("i2c probe: i2c1 not ready");
        return;
    }

    uint8_t found = 0;
    uint8_t addrs[12];
    uint8_t addr_count = 0;
    bool touch_found = false;

    /* 0x08..0x77 is the usable 7-bit range; the rest is reserved.
     *
     * Presence test is a 1-byte read, not a zero-length write. The zero-length
     * write is the textbook probe, but nRF TWIM has no real support for a
     * transfer with no data phase -- depending on the Zephyr revision it
     * errors, asserts, or stalls the peripheral. A 1-byte read drives a proper
     * address phase and one clocked byte, which every controller handles, and
     * the discarded byte cannot disturb a device: a read never writes a
     * register. */
    for (uint16_t addr = 0x08; addr <= 0x77; addr++) {
        uint8_t scratch;

        if (i2c_read(bus, &scratch, sizeof(scratch), (uint8_t)addr) == 0) {
            LOG_INF("i2c probe: 0x%02x responded%s", addr, known_device(addr));
            found++;
            if (addr_count < ARRAY_SIZE(addrs)) {
                addrs[addr_count++] = (uint8_t)addr;
            }
            if (addr == 0x15 || addr == 0x38 || addr == 0x14 || addr == 0x5D) {
                touch_found = true;
            }
        }
    }

    LOG_INF("i2c probe: sensor driver %s", sensor_state());

    if (found == 0) {
        LOG_WRN("i2c probe: nothing answered on i2c1 at all");
    } else if (found == 1) {
        LOG_INF("i2c probe: 1 device -- if that is only 0x39, touch is NOT wired");
    } else {
        LOG_INF("i2c probe: %u devices", found);
    }

    /* Same answer on the glass. "TOUCH" / "NO TOUCH" is the whole point, so
     * say it in words rather than making anyone decode hex on a 1.69" panel. */
    char line[96];
    int n = snprintf(line, sizeof(line), "i2c %u:", found);

    for (uint8_t i = 0; i < addr_count && n < (int)sizeof(line); i++) {
        n += snprintf(line + n, sizeof(line) - n, " %02x", addrs[i]);
    }
    if (n < (int)sizeof(line)) {
        snprintf(line + n, sizeof(line) - n, "\n%s\n%s", sensor_state(),
                 touch_found ? "TOUCH FOUND" : "NO TOUCH");
    }

    publish_result(line);
}

/*
 * Repeat rather than scan once. USB CDC takes a couple of seconds to enumerate
 * and the console is attached by hand, so a single scan at boot is almost
 * guaranteed to be logged before anyone is reading. Re-running means the answer
 * shows up within one period of opening the port.
 */
static void probe_work_fn(struct k_work *work) {
    ARG_UNUSED(work);
    probe_once();
    k_work_reschedule(k_work_delayable_from_work(work),
                      K_MSEC(CONFIG_ROBA_PROSPECTOR_I2C_PROBE_PERIOD_MS));
}

static K_WORK_DELAYABLE_DEFINE(probe_work, probe_work_fn);

static int roba_prospector_i2c_probe_init(void) {
    k_work_schedule(&probe_work, K_MSEC(CONFIG_ROBA_PROSPECTOR_I2C_PROBE_DELAY_MS));
    return 0;
}

SYS_INIT(roba_prospector_i2c_probe_init, APPLICATION, 99);
