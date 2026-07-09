/*
 * Peripheral display support on the damex ESB transport.
 *
 * The stock peripheral status screens (e.g. the nice!view widgets) link
 * against the BLE split API's zmk_split_bt_peripheral_is_connected(). With
 * CONFIG_ZMK_SPLIT_BLE=n that symbol is never built, so provide it here.
 *
 * ESB is connectionless: the transport itself reports ALL_CONNECTED (see
 * zmk-feature-split-esb peripheral.c), so mirror that and always report a
 * live connection.
 */

#include <stdbool.h>

#include <zmk/split/bluetooth/peripheral.h>

bool zmk_split_bt_peripheral_is_connected(void) { return true; }
