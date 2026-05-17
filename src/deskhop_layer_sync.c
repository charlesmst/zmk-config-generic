#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <zmk/event_manager.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/keymap.h>
#include <dt-bindings/zmk/hid_usage.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if IS_ENABLED(CONFIG_ZMK_HID_INDICATORS)

static int deskhop_layer_sync_listener(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ev = as_zmk_hid_indicators_changed(eh);

    if (ev == NULL) {
        return ZMK_EV_EVENT_BUBBLE;
    }

#if IS_ENABLED(CONFIG_ZMK_SPLIT) && !IS_ENABLED(CONFIG_ZMK_SPLIT_ROLE_CENTRAL)
    return ZMK_EV_EVENT_BUBBLE;
#else
    const bool sync_on = (ev->indicators & CONFIG_ROBA_DESKHOP_LAYER_SYNC_LED_MASK) != 0;
    const uint8_t target_layer =
        sync_on ? CONFIG_ROBA_DESKHOP_LAYER_WHEN_LED_ON : CONFIG_ROBA_DESKHOP_LAYER_WHEN_LED_OFF;

    LOG_INF("DeskHop sync event indicators=0x%02x target=%d highest=%d", ev->indicators,
            target_layer, zmk_keymap_highest_layer_active());

#if IS_ENABLED(CONFIG_ROBA_DESKHOP_LAYER_SYNC_DEBUG_KEY)
    zmk_hid_keyboard_press((zmk_key_t)CONFIG_ROBA_DESKHOP_LAYER_SYNC_DEBUG_KEY_USAGE);
    zmk_endpoints_send_report(HID_USAGE_KEY);
    zmk_hid_keyboard_release((zmk_key_t)CONFIG_ROBA_DESKHOP_LAYER_SYNC_DEBUG_KEY_USAGE);
    zmk_endpoints_send_report(HID_USAGE_KEY);
#endif

    if (zmk_keymap_highest_layer_active() != target_layer) {
        zmk_keymap_layer_to(target_layer);
        LOG_DBG("DeskHop sync indicators=0x%02x -> layer=%d", ev->indicators, target_layer);
    }

    return ZMK_EV_EVENT_BUBBLE;
#endif
}

ZMK_LISTENER(roba_deskhop_layer_sync, deskhop_layer_sync_listener);
ZMK_SUBSCRIPTION(roba_deskhop_layer_sync, zmk_hid_indicators_changed);

#endif
