/*
 * osb_pad.h - (SAVEDLG-TOUCH) the on-screen buttons as a controller while an overlay dialog has the input (the save-data
 * dialog, the IME card: overlay_dialog_active). Those dialogs read the controller's ImGui keys (savedlg_ui.cpp's feed:
 * Cross = choose, Circle = back, D-pad / left stick = move); window.c used to hand a dialog's touch events straight to
 * the overlay, so a finger on the on-screen Cross or D-pad pressed nothing there (owner, Galaxy Tab S7). window.c turns
 * the on-screen buttons' state (touch_controls.h: the Vita's button bits, the left stick's 0-255 axes) into the SDL
 * gamepad events the overlay already takes from a real pad, through these helpers.
 * Plain C, header-only: runtime/tests/savedlg_touch_test.cpp checks it with the real dialog.
 */
#ifndef RECOMP_OSB_PAD_H
#define RECOMP_OSB_PAD_H

#include <SDL3/SDL_gamepad.h>
#include "touch_controls.h"

/* the controller button an on-screen button stands for (the Vita's layout on SDL's positional names), -1 for none */
static inline int osb_pad_button(unsigned bit) {
	switch (bit) {
		case OSB_BIT_CROSS: return SDL_GAMEPAD_BUTTON_SOUTH;
		case OSB_BIT_CIRCLE: return SDL_GAMEPAD_BUTTON_EAST;
		case OSB_BIT_SQUARE: return SDL_GAMEPAD_BUTTON_WEST;
		case OSB_BIT_TRIANGLE: return SDL_GAMEPAD_BUTTON_NORTH;
		case OSB_BIT_UP: return SDL_GAMEPAD_BUTTON_DPAD_UP;
		case OSB_BIT_DOWN: return SDL_GAMEPAD_BUTTON_DPAD_DOWN;
		case OSB_BIT_LEFT: return SDL_GAMEPAD_BUTTON_DPAD_LEFT;
		case OSB_BIT_RIGHT: return SDL_GAMEPAD_BUTTON_DPAD_RIGHT;
		case OSB_BIT_L: return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
		case OSB_BIT_R: return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
		case OSB_BIT_START: return SDL_GAMEPAD_BUTTON_START;
		case OSB_BIT_SELECT: return SDL_GAMEPAD_BUTTON_BACK;
		default: return -1;
	}
}

/* an on-screen stick's 0-255 axis (128 the centre) as an SDL axis value */
static inline int osb_pad_axis(unsigned v) { int a = ((int)v - 128) * 256; return a < -32768 ? -32768 : a > 32767 ? 32767 : a; }

/* the state handed to the overlay so far, and what changed since: each button that went down or up and each left-stick axis
 * that moved, as calls to send_button(SDL button, down) / send_axis(SDL axis, value) */
typedef struct { unsigned bits; int lx, ly; } OsbPadFed;
static inline void osb_pad_reset(OsbPadFed *f) { f->bits = 0; f->lx = f->ly = 0; }
static inline void osb_pad_feed(OsbPadFed *f, unsigned bits, int stick_on, unsigned sx, unsigned sy,
                                void (*send_button)(int button, int down), void (*send_axis)(int axis, int value)) {
	const unsigned diff = f->bits ^ bits;
	for (unsigned b = 1; b; b <<= 1) if (diff & b) { const int btn = osb_pad_button(b); if (btn >= 0) send_button(btn, (bits & b) != 0); }
	f->bits = bits;
	const int lx = stick_on ? osb_pad_axis(sx) : 0, ly = stick_on ? osb_pad_axis(sy) : 0;
	if (lx != f->lx) { send_axis(SDL_GAMEPAD_AXIS_LEFTX, lx); f->lx = lx; }
	if (ly != f->ly) { send_axis(SDL_GAMEPAD_AXIS_LEFTY, ly); f->ly = ly; }
}
#endif
