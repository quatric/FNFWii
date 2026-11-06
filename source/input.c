#include "input.h"

#include <gccore.h>
#include <input_wii.h>
#include <string.h>
#include <wiiuse/wpad.h>

#include "util.h"

/* USB HID usage codes */
#define K_A 0x04
#define K_D 0x07
#define K_R 0x15
#define K_S 0x16
#define K_W 0x1A
#define K_ENTER 0x28
#define K_ESC 0x29
#define K_BACKSPACE 0x2A
#define K_SPACE 0x2C
#define K_RIGHT 0x4F
#define K_LEFT 0x50
#define K_DOWN 0x51
#define K_UP 0x52

static u32 held, pressed, prev_held;
static u8 lanes, lanes_prev;
static u32 raw_down;
static u32 nav_out, nav_dir;
static double nav_next;
static const char *hint = "Wii Remote";

void in_init(void)
{
    INPUT_Init();
    INPUT_SetScreenSize(640, 480);
    INPUT_SetCursorMode(0);
    INPUT_KeyboardMapClear(); /* the keyboard is read key by key below */
}

static bool sideways_remote[4];

void in_update(void)
{
    INPUT_Update();
    u32 f = INPUT_ButtonsHeld(); /* every device except bare Wii Remotes (ignored below) */
    raw_down = INPUT_ButtonsDown();
    u32 h = 0;
    u8 l = 0;

    /* Folded controllers: Classic, GameCube, Pro, Remote+Nunchuk (upright) */
    if (f & WPAD_BUTTON_LEFT) h |= ACT_LEFT, l |= 1;
    if (f & WPAD_BUTTON_DOWN) h |= ACT_DOWN, l |= 2;
    if (f & WPAD_BUTTON_UP) h |= ACT_UP, l |= 4;
    if (f & WPAD_BUTTON_RIGHT) h |= ACT_RIGHT, l |= 8;
    if (f & (WPAD_BUTTON_1 | INPUT_BUTTON_Z)) l |= 1;
    if (f & (WPAD_BUTTON_B | INPUT_BUTTON_C)) l |= 2;
    if (f & WPAD_BUTTON_2) l |= 4;
    if (f & WPAD_BUTTON_A) l |= 8;
    if (f & WPAD_BUTTON_A) h |= ACT_ACCEPT;
    if (f & WPAD_BUTTON_B) h |= ACT_BACK;
    if (f & WPAD_BUTTON_PLUS) h |= ACT_PAUSE;
    if (f & WPAD_BUTTON_HOME) h |= ACT_HOME;
    if (f & WPAD_BUTTON_MINUS) h |= ACT_RESET;

    /* Bare Wii Remotes are held sideways: their buttons are read here,
     * rotated, and kept out of libwiiinput's mask. */
    for (int ch = 0; ch < 4; ch++) {
        u32 type;
        bool bare = WPAD_Probe(ch, &type) == WPAD_ERR_NONE && type == WPAD_EXP_NONE;
        if (bare != sideways_remote[ch]) {
            sideways_remote[ch] = bare;
            INPUT_IgnoreWiimoteButtons(ch, bare ? 0xFFFF : 0);
        }
        if (!bare) continue;
        u32 b = WPAD_ButtonsHeld(ch);
        if (b & WPAD_BUTTON_UP) h |= ACT_LEFT, l |= 1;     /* physical left */
        if (b & WPAD_BUTTON_LEFT) h |= ACT_DOWN, l |= 2;   /* physical down */
        if (b & WPAD_BUTTON_RIGHT) h |= ACT_UP, l |= 4;    /* physical up */
        if (b & WPAD_BUTTON_DOWN) h |= ACT_RIGHT, l |= 8;  /* physical right */
        if (b & WPAD_BUTTON_B) l |= 1;
        if (b & WPAD_BUTTON_A) l |= 2;
        if (b & WPAD_BUTTON_1) l |= 4, h |= ACT_BACK;
        if (b & WPAD_BUTTON_2) l |= 8, h |= ACT_ACCEPT;
        if (b & WPAD_BUTTON_A) h |= ACT_ACCEPT;
        if (b & WPAD_BUTTON_PLUS) h |= ACT_PAUSE;
        if (b & WPAD_BUTTON_MINUS) h |= ACT_RESET;
        if (b & WPAD_BUTTON_HOME) h |= ACT_HOME;
        u32 d = WPAD_ButtonsDown(ch);
        /* libwiiupdate's popup wants upright A/B */
        if (d & WPAD_BUTTON_2) raw_down |= WPAD_BUTTON_A;
        if (d & WPAD_BUTTON_1) raw_down |= WPAD_BUTTON_B;
        if (d & WPAD_BUTTON_A) raw_down |= WPAD_BUTTON_A;
        if (d & WPAD_BUTTON_HOME) raw_down |= WPAD_BUTTON_HOME;
    }

    if (INPUT_KeyboardConnected()) {
        /* the game's default binds: WASD and the arrows */
        if (INPUT_KeyHeld(K_LEFT) || INPUT_KeyHeld(K_A)) l |= 1;
        if (INPUT_KeyHeld(K_DOWN) || INPUT_KeyHeld(K_S)) l |= 2;
        if (INPUT_KeyHeld(K_UP) || INPUT_KeyHeld(K_W)) l |= 4;
        if (INPUT_KeyHeld(K_RIGHT) || INPUT_KeyHeld(K_D)) l |= 8;
        if (INPUT_KeyHeld(K_LEFT)) h |= ACT_LEFT;
        if (INPUT_KeyHeld(K_DOWN)) h |= ACT_DOWN;
        if (INPUT_KeyHeld(K_UP)) h |= ACT_UP;
        if (INPUT_KeyHeld(K_RIGHT)) h |= ACT_RIGHT;
        if (INPUT_KeyHeld(K_ENTER) || INPUT_KeyHeld(K_SPACE)) h |= ACT_ACCEPT;
        if (INPUT_KeyHeld(K_ENTER)) h |= ACT_PAUSE;
        if (INPUT_KeyHeld(K_ESC) || INPUT_KeyHeld(K_BACKSPACE)) h |= ACT_BACK;
        if (INPUT_KeyHeld(K_ESC)) h |= ACT_PAUSE;
        if (INPUT_KeyHeld(K_R)) h |= ACT_RESET;
        if (INPUT_KeyPressed(K_ENTER)) raw_down |= WPAD_BUTTON_A;
        if (INPUT_KeyPressed(K_ESC) || INPUT_KeyPressed(K_BACKSPACE)) raw_down |= WPAD_BUTTON_B;
    }

    /* Analog sticks steer menus */
    float sx, sy;
    if (INPUT_Stick(INPUT_STICK_LEFT, &sx, &sy, NULL) > 0.6f) {
        if (sx < -0.6f) h |= ACT_LEFT;
        if (sx > 0.6f) h |= ACT_RIGHT;
        if (sy > 0.6f) h |= ACT_UP;
        if (sy < -0.6f) h |= ACT_DOWN;
    }

    const char *src = INPUT_Source();
    if (src && src[0]) hint = src;

    prev_held = held;
    held = h;
    pressed = held & ~prev_held;
    lanes_prev = lanes;
    lanes = l;

    /* key repeat for menus: first after 0.4 s, then every 0.08 s */
    u32 dirs = held & (ACT_LEFT | ACT_DOWN | ACT_UP | ACT_RIGHT);
    double now = now_ms();
    nav_out = pressed & dirs;
    if (dirs != nav_dir) {
        nav_dir = dirs;
        nav_next = now + 400;
    } else if (dirs && now >= nav_next) {
        nav_out = dirs;
        nav_next = now + 80;
    }
}

/* autotest: actions pressed for this one frame */
void in_inject(u32 a)
{
    pressed |= a & ~held;
    held |= a;
    if (a & ACT_ACCEPT) raw_down |= WPAD_BUTTON_A;
    if (a & ACT_BACK) raw_down |= WPAD_BUTTON_B;
    nav_out |= a & (ACT_LEFT | ACT_DOWN | ACT_UP | ACT_RIGHT);
}

u32 in_held(void) { return held; }
u32 in_pressed(void) { return pressed; }
u32 in_nav(void) { return nav_out; }
u8 in_lanes_held(void) { return lanes; }
u8 in_lanes_pressed(void) { return lanes & ~lanes_prev; }
u8 in_lanes_released(void) { return lanes_prev & ~lanes; }
u32 in_raw_down(void) { return raw_down; }
const char *in_layout_hint(void) { return hint; }
