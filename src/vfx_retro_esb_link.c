/*
 * Feed real ESB link status into the zmk-vfx-retro status screen.
 *
 * vfx-retro ships a __weak vfx_retro_link_get() that returns a placeholder
 * (full signal, channel 0). Override it with this peripheral's own
 * central-link RSSI and hop channel from the damex zmk-feature-split-esb
 * transport (zmk_split_esb_get_status), so the retro nice!view screen shows
 * the live signal strength and current channel.
 *
 * struct vfx_retro_link is redeclared here because its definition lives in
 * the vfx-retro module's private src/link.h, which is not on this module's
 * include path. Keep this layout in sync with that header.
 */

#include <stdbool.h>
#include <stdint.h>

#include <zmk_split_esb.h>

struct vfx_retro_link {
    bool connected;
    bool searching;
    int8_t rssi_dbm;
    uint8_t channel;
};

struct vfx_retro_link vfx_retro_link_get(void) {
    struct zmk_split_esb_status status;
    zmk_split_esb_get_status(&status);

    return (struct vfx_retro_link){
        .connected = !status.searching,
        .searching = status.searching,
        .rssi_dbm = status.rssi_dbm,
        .channel = status.channel,
    };
}
