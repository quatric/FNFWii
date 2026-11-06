/* Controls, on top of libwiiinput.
 *
 * Note lanes (left, down, up, right):
 *   Wii Remote held sideways : D-pad, or B / A / 1 / 2
 *   Wii Remote + Nunchuk     : D-pad, or Z / C / A / B
 *   Classic, GameCube, Wii U Pro, DualShock 3, USB pads:
 *                              D-pad, or Y / B / X / A (the face diamond)
 *   USB keyboard             : arrows, D F J K, or W A S D
 */
#ifndef INPUT_H
#define INPUT_H

#include <gctypes.h>
#include <stdbool.h>

enum {
    ACT_LEFT = 1 << 0,
    ACT_DOWN = 1 << 1,
    ACT_UP = 1 << 2,
    ACT_RIGHT = 1 << 3,
    ACT_ACCEPT = 1 << 4,
    ACT_BACK = 1 << 5,
    ACT_PAUSE = 1 << 6,
    ACT_HOME = 1 << 7,
    ACT_RESET = 1 << 8,   /* restart song: minus / R key */
};

void in_init(void);
void in_update(void);
u32  in_held(void);
u32  in_pressed(void);
/* Menu navigation with key repeat (ACT_LEFT..ACT_RIGHT). */
u32  in_nav(void);
/* Note lanes as bits 0..3. */
u8   in_lanes_held(void);
u8   in_lanes_pressed(void);
u8   in_lanes_released(void);
u32  in_raw_down(void);   /* WPAD_BUTTON_* bits pressed this frame, for libwiiupdate */
const char *in_layout_hint(void); /* "Wii Remote" / "Classic Controller" / ... */

#endif
