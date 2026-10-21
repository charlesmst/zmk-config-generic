/*
 * Keychron B1 Pro (US) bindings for roBakesb.keymap -- positions 48-125.
 *
 * The keymap is one file for every roBakesb target. A Keychron on the link adds
 * 78 positions (its switch, then its 77 keys) after the roBa's 43 and the lariska's 5 (see
 * roBakesb_keychron_transform.dtsi), so each layer ends with one KEYCHRON_*
 * macro, the same way it already ends with a LARISKA_BTNS* one. When the
 * Keychron is not on the link (no roBakesb_keychron_link shield) ROBA_KEYCHRON
 * is undefined, every macro here is empty, and the keymap is the one it was
 * before the Keychron existed.
 *
 * Which macro a layer takes:
 *   KEYCHRON_MAC     Mac base. Win/Alt swap to Option/Command, as Keychron's own
 *                    firmware does in Mac mode, so the keycaps' physical spots
 *                    are ctrl, opt, cmd ... cmd.
 *   KEYCHRON_WIN     Windows base.
 *   KEYCHRON_GAME    Gaming base: plain Windows layout, since `&to GAMIN` leaves
 *                    no layer underneath for &trans to fall through to.
 *   KEYCHRON_SYSTEM  the Fn layer (SUPER, "System"): media on F7-F12, brightness
 *                    on F1/F2, and 1/2/3 pick Mac/Windows/Gaming.
 *   KEYCHRON_TRANS   every other layer: &trans, so a chord held on the roBa or
 *                    the lariska (tiling, fast scroll, ...) still lets the
 *                    Keychron type through to its base.
 *
 * Fn is `&mo SUPER` on every base layer. The first position of each macro is
 * the Mac/Win slide switch, "held" while it sits on Win. Every base layer binds
 * it to `&kc_game_switch` (roBakesb.keymap): with the switch on Win, Windows
 * comes up with Gaming on top, including every time DeskHop returns to the
 * Windows machine; Mac ignores it. Mac or Windows itself still follows DeskHop
 * or Fn+1/2/3. The BT and 2.4G switches are not positions.
 *
 * Only the US/ANSI board is mapped. ISO and JIS have their own shields in
 * Keychron/zmk (keychron_b1_uk, keychron_b1_jis) with extra keys and a
 * different matrix.
 */

#pragma once

#ifdef ROBA_KEYCHRON

/* Mac base */
#define KEYCHRON_MAC \
    /* Mac/Win switch, held while on Win */ \
    &kc_game_switch \
    /* Esc, F1-F12, Del */ \
    &kp ESC       &kp F1        &kp F2        &kp F3        &kp F4        &kp F5        &kp F6        &kp F7        &kp F8        &kp F9        &kp F10       &kp F11       &kp F12       &kp DEL \
    /* number row */ \
    &kp GRAVE     &kp N1        &kp N2        &kp N3        &kp N4        &kp N5        &kp N6        &kp N7        &kp N8        &kp N9        &kp N0        &kp MINUS     &kp EQUAL     &kp BSPC \
    /* Tab row */ \
    &kp TAB       &kp Q         &kp W         &kp E         &kp R         &kp T         &kp Y         &kp U         &kp I         &kp O         &kp P         &kp LBKT      &kp RBKT      &kp BSLH \
    /* home row */ \
    &kp CAPS      &kp A         &kp S         &kp D         &kp F         &kp G         &kp H         &kp J         &kp K         &kp L         &kp SEMI      &kp SQT       &kp RET \
    /* shift row */ \
    &kp LSHFT     &kp Z         &kp X         &kp C         &kp V         &kp B         &kp N         &kp M         &kp COMMA     &kp DOT       &kp FSLH      &kp RSHFT \
    /* bottom row; arrows last: left, up, down, right */ \
    &kp LCTRL     &kp LALT      &kp LGUI      &kp SPACE     &kp RGUI      &mo SUPER     &kp LEFT      &kp UP        &kp DOWN      &kp RIGHT

/* Windows base */
#define KEYCHRON_WIN \
    /* Mac/Win switch, held while on Win */ \
    &kc_game_switch \
    /* Esc, F1-F12, Del */ \
    &kp ESC       &kp F1        &kp F2        &kp F3        &kp F4        &kp F5        &kp F6        &kp F7        &kp F8        &kp F9        &kp F10       &kp F11       &kp F12       &kp DEL \
    /* number row */ \
    &kp GRAVE     &kp N1        &kp N2        &kp N3        &kp N4        &kp N5        &kp N6        &kp N7        &kp N8        &kp N9        &kp N0        &kp MINUS     &kp EQUAL     &kp BSPC \
    /* Tab row */ \
    &kp TAB       &kp Q         &kp W         &kp E         &kp R         &kp T         &kp Y         &kp U         &kp I         &kp O         &kp P         &kp LBKT      &kp RBKT      &kp BSLH \
    /* home row */ \
    &kp CAPS      &kp A         &kp S         &kp D         &kp F         &kp G         &kp H         &kp J         &kp K         &kp L         &kp SEMI      &kp SQT       &kp RET \
    /* shift row */ \
    &kp LSHFT     &kp Z         &kp X         &kp C         &kp V         &kp B         &kp N         &kp M         &kp COMMA     &kp DOT       &kp FSLH      &kp RSHFT \
    /* bottom row; arrows last: left, up, down, right */ \
    &kp LCTRL     &kp LGUI      &kp LALT      &kp SPACE     &kp RALT      &mo SUPER     &kp LEFT      &kp UP        &kp DOWN      &kp RIGHT

/* Gaming base */
#define KEYCHRON_GAME \
    /* Mac/Win switch, held while on Win */ \
    &kc_game_switch \
    /* Esc, F1-F12, Del */ \
    &kp ESC       &kp F1        &kp F2        &kp F3        &kp F4        &kp F5        &kp F6        &kp F7        &kp F8        &kp F9        &kp F10       &kp F11       &kp F12       &kp DEL \
    /* number row */ \
    &kp GRAVE     &kp N1        &kp N2        &kp N3        &kp N4        &kp N5        &kp N6        &kp N7        &kp N8        &kp N9        &kp N0        &kp MINUS     &kp EQUAL     &kp BSPC \
    /* Tab row */ \
    &kp TAB       &kp Q         &kp W         &kp E         &kp R         &kp T         &kp Y         &kp U         &kp I         &kp O         &kp P         &kp LBKT      &kp RBKT      &kp BSLH \
    /* home row */ \
    &kp CAPS      &kp A         &kp S         &kp D         &kp F         &kp G         &kp H         &kp J         &kp K         &kp L         &kp SEMI      &kp SQT       &kp RET \
    /* shift row */ \
    &kp LSHFT     &kp Z         &kp X         &kp C         &kp V         &kp B         &kp N         &kp M         &kp COMMA     &kp DOT       &kp FSLH      &kp RSHFT \
    /* bottom row; arrows last: left, up, down, right */ \
    &kp LCTRL     &kp LGUI      &kp LALT      &kp SPACE     &kp RALT      &mo SUPER     &kp LEFT      &kp UP        &kp DOWN      &kp RIGHT

/* Fn layer (System). Fn + the F-row does what the B1's keycaps print, as the
 * stock firmware does: brightness, Mission Control / Task View, Launchpad /
 * File Explorer, Spotlight / Search, lock screen, then media and volume. Where
 * the two machines differ the key is &os_kp MAC WIN, chosen by whether Windows
 * is the active base. Also from stock: Fn + arrows for Home, PgUp, PgDn, End;
 * Fn + I for Insert; Fn + \ for a screenshot; Fn + Right Shift for the emoji
 * picker. Fn + 1/2/3 still set the base to Mac, Windows or Gaming. */
#define KEYCHRON_SYSTEM \
    /* Mac/Win switch, held while on Win */ \
    &trans \
    /* Esc, F1-F12, Del */ \
    &trans \
    &kp C_BRI_DN \
    &kp C_BRI_UP \
    &os_kp C_AC_DESKTOP_SHOW_ALL_WINDOWS LG(TAB) \
    &os_kp C_AC_DESKTOP_SHOW_ALL_APPLICATIONS LG(E) \
    &os_kp C_AC_SEARCH LG(S) \
    &os_kp LC(LG(Q)) LG(L) \
    &kp C_PREV    &kp C_PP      &kp C_NEXT    &kp C_MUTE    &kp C_VOL_DN  &kp C_VOL_UP  &trans \
    /* number row */ \
    &trans        &to DEFAU     &to WINDO     &to GAMIN     &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans \
    /* Tab row: I = Insert, \ = screenshot */ \
    &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &kp INS       &trans        &trans        &trans        &trans \
    &os_kp LG(LS(N4)) LG(LS(S)) \
    /* home row */ \
    &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans \
    /* shift row: Right Shift = emoji picker */ \
    &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans \
    &os_kp LC(LG(SPACE)) LG(DOT) \
    /* bottom row; arrows last: left, up, down, right -> Home, PgUp, PgDn, End */ \
    &trans        &trans        &trans        &trans        &trans        &trans        &kp HOME      &kp PG_UP     &kp PG_DN     &kp END

/* Every other layer */
#define KEYCHRON_TRANS \
    /* Mac/Win switch, held while on Win */ \
    &trans        \
    /* Esc, F1-F12, Del */ \
    &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans \
    /* number row */ \
    &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans \
    /* Tab row */ \
    &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans \
    /* home row */ \
    &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans \
    /* shift row */ \
    &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans \
    /* bottom row; arrows last: left, up, down, right */ \
    &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans        &trans

#else /* !ROBA_KEYCHRON */

#define KEYCHRON_MAC
#define KEYCHRON_WIN
#define KEYCHRON_GAME
#define KEYCHRON_SYSTEM
#define KEYCHRON_TRANS

#endif /* ROBA_KEYCHRON */
