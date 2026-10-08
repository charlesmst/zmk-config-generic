# Central-side display widgets size their per-peripheral arrays with
# ZMK_SPLIT_BLE_PERIPHERAL_COUNT. zmk/app/include/zmk/ble.h only defines it
# under `#if ZMK_BLE_IS_CENTRAL`, i.e. ZMK_SPLIT_BLE && ZMK_SPLIT_ROLE_CENTRAL,
# so on an ESB-only central the macro is absent however the Kconfig is set --
# declaring CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS is necessary but not
# sufficient. Supply the macro globally instead.
#
# No redefinition risk: ROBA_ESB_CENTRAL_DISPLAY_COMPAT requires !ZMK_SPLIT_BLE,
# which is exactly when ble.h leaves it undefined.
if(CONFIG_ROBA_ESB_CENTRAL_DISPLAY_COMPAT)
  zephyr_compile_definitions(
      ZMK_SPLIT_BLE_PERIPHERAL_COUNT=CONFIG_ZMK_SPLIT_BLE_CENTRAL_PERIPHERALS)
endif()

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_ROBA_PMW3610_PERIPHERAL_KEYMAP_COMPAT src/pmw3610_peripheral_keymap_compat.c)
zephyr_library_sources_ifdef(CONFIG_ROBA_ESB_PERIPHERAL_DISPLAY_COMPAT src/esb_peripheral_display_compat.c)
zephyr_library_sources_ifdef(CONFIG_ROBA_ESB_CENTRAL_DISPLAY_COMPAT src/esb_central_display_compat.c)
zephyr_library_sources_ifdef(CONFIG_ROBA_PERIPHERAL_BATTERY_LED src/peripheral_battery_led.c)
zephyr_library_sources_ifdef(CONFIG_ROBA_SELF_BATTERY_LED src/self_battery_led.c)
zephyr_library_sources_ifdef(CONFIG_ROBA_DONGLE_BATTERY_DISPLAY src/dongle_battery_display.c)
zephyr_library_sources_ifdef(CONFIG_ROBA_DONGLE_BATTERY_DISPLAY src/dongle_icons.c)
zephyr_library_sources_ifdef(CONFIG_ROBA_DONGLE_BATTERY_DISPLAY src/os_logos.c)
zephyr_library_sources_ifdef(CONFIG_ROBA_VFX_RETRO_ESB_LINK src/vfx_retro_esb_link.c)
zephyr_library_sources_ifdef(CONFIG_ROBA_INPUT_PROCESSOR_TAP_CLICK src/input_processor_tap_click.c)
zephyr_library_sources_ifdef(CONFIG_ROBA_BEHAVIOR_AUTOCLICK src/behavior_autoclick.c)
zephyr_library_sources_ifdef(CONFIG_ROBA_INPUT_PROCESSOR_CIRQUE_GESTURES src/input_processors/cirque_gestures.c)