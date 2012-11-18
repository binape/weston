/*
 * Copyright © 2012 Intel Corporation
 *
 * Permission to use, copy, modify, distribute, and sell this software and
 * its documentation for any purpose is hereby granted without fee, provided
 * that the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the copyright holders not be used in
 * advertising or publicity pertaining to distribution of the software
 * without specific, written prior permission.  The copyright holders make
 * no representations about the suitability of this software for any
 * purpose.  It is provided "as is" without express or implied warranty.
 *
 * THE COPYRIGHT HOLDERS DISCLAIM ALL WARRANTIES WITH REGARD TO THIS
 * SOFTWARE, INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS, IN NO EVENT SHALL THE COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * SPECIAL, INDIRECT OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER
 * RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF
 * CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <linux/input.h>

#include "window.h"
#include "keyboard-utils.h"
#include "input-method-client-protocol.h"

enum compose_state {
	state_normal,
	state_compose
};

struct compose_seq {
	uint32_t keys[4];

	const char *text;
};

struct simple_im {
	struct input_method *input_method;
	struct input_method_context *context;
	struct display *display;
	struct wl_keyboard *keyboard;
	struct keyboard_input *keyboard_input;
	enum compose_state compose_state;
	struct compose_seq compose_seq;
};

static const struct compose_seq compose_seqs[] = {
	{ { XKB_KEY_quotedbl, XKB_KEY_A, 0 },  "Ä" },
	{ { XKB_KEY_quotedbl, XKB_KEY_O, 0 },  "Ö" },
	{ { XKB_KEY_quotedbl, XKB_KEY_U, 0 },  "Ü" },
	{ { XKB_KEY_quotedbl, XKB_KEY_a, 0 },  "ä" },
	{ { XKB_KEY_quotedbl, XKB_KEY_o, 0 },  "ö" },
	{ { XKB_KEY_quotedbl, XKB_KEY_u, 0 },  "ü" },
	{ { XKB_KEY_apostrophe, XKB_KEY_A, 0 },  "Á" },
	{ { XKB_KEY_apostrophe, XKB_KEY_a, 0 },  "á" },
	{ { XKB_KEY_O, XKB_KEY_C, 0 },  "©" },
	{ { XKB_KEY_O, XKB_KEY_R, 0 },  "®" },
	{ { XKB_KEY_s, XKB_KEY_s, 0 },  "ß" },
};

static const uint32_t ignore_keys_on_compose[] = {
	XKB_KEY_Shift_L,
	XKB_KEY_Shift_R
};

static void
input_method_context_surrounding_text(void *data,
				      struct input_method_context *context,
				      const char *text,
				      uint32_t cursor,
				      uint32_t anchor)
{
	fprintf(stderr, "Surrounding text updated: %s\n", text);
}

static void
input_method_context_reset(void *data,
			   struct input_method_context *context)
{
	struct simple_im *keyboard = data;

	fprintf(stderr, "Reset pre-edit buffer\n");

	keyboard->compose_state = state_normal;
}

static const struct input_method_context_listener input_method_context_listener = {
	input_method_context_surrounding_text,
	input_method_context_reset
};

static void
input_method_keyboard_keymap(void *data,
			     struct wl_keyboard *wl_keyboard,
			     uint32_t format,
			     int32_t fd,
			     uint32_t size)
{
	struct simple_im *keyboard = data;

	keyboard_input_handle_keymap(keyboard->keyboard_input, format, fd, size);
}

static void
input_method_keyboard_key(void *data,
			  struct wl_keyboard *wl_keyboard,
			  uint32_t serial,
			  uint32_t time,
			  uint32_t key,
			  uint32_t state_w)
{
	struct simple_im *keyboard = data;

	keyboard_input_handle_key(keyboard->keyboard_input, serial,
				  time, key, state_w);
}

static void
input_method_keyboard_modifiers(void *data,
				struct wl_keyboard *wl_keyboard,
				uint32_t serial,
				uint32_t mods_depressed,
				uint32_t mods_latched,
				uint32_t mods_locked,
				uint32_t group)
{
	struct simple_im *keyboard = data;
	struct input_method_context *context = keyboard->context;

	keyboard_input_handle_modifiers(keyboard->keyboard_input, serial,
					mods_depressed, mods_latched, mods_locked, group);

	input_method_context_modifiers(context, serial,
				       mods_depressed, mods_depressed,
				       mods_latched, group);
}

static const struct wl_keyboard_listener input_method_keyboard_listener = {
	input_method_keyboard_keymap,
	NULL, /* enter */
	NULL, /* leave */
	input_method_keyboard_key,
	input_method_keyboard_modifiers
};

static void
input_method_activate(void *data,
		      struct input_method *input_method,
		      struct input_method_context *context)
{
	struct simple_im *keyboard = data;

	if (keyboard->context)
		input_method_context_destroy(keyboard->context);

	keyboard->compose_state = state_normal;

	keyboard->context = context;
	input_method_context_add_listener(context,
					  &input_method_context_listener,
					  keyboard);
	keyboard->keyboard = input_method_context_grab_keyboard(context);
	wl_keyboard_add_listener(keyboard->keyboard,
				 &input_method_keyboard_listener,
				 keyboard);
}

static void
input_method_deactivate(void *data,
			struct input_method *input_method,
			struct input_method_context *context)
{
	struct simple_im *keyboard = data;

	if (!keyboard->context)
		return;

	input_method_context_destroy(keyboard->context);
	keyboard->context = NULL;
}

static const struct input_method_listener input_method_listener = {
	input_method_activate,
	input_method_deactivate
};

static void
global_handler(struct display *display, uint32_t name,
	       const char *interface, uint32_t version, void *data)
{
	struct simple_im *keyboard = data;

	if (!strcmp(interface, "input_method")) {
		keyboard->input_method =
			display_bind(display, name,
				     &input_method_interface, 1);
		input_method_add_listener(keyboard->input_method, &input_method_listener, keyboard);
	}
}

static int
compare_compose_keys(const void *c1, const void *c2)
{
	const struct compose_seq *cs1 = c1;
	const struct compose_seq *cs2 = c2;
	int i;

	for (i = 0; cs1->keys[i] != 0 && cs2->keys[i] != 0; i++) {
		if (cs1->keys[i] != cs2->keys[i])
			return cs1->keys[i] - cs2->keys[i];
	}

	if (cs1->keys[i] == cs2->keys[i]
	    || cs1->keys[i] == 0)
		return 0;

	return cs1->keys[i] - cs2->keys[i];
}

static void
simple_im_key_handler(struct keyboard_input *keyboard_input,
		      uint32_t time, uint32_t key, uint32_t sym,
		      enum wl_keyboard_key_state state, void *data)
{
	struct simple_im *keyboard = data;
	struct input_method_context *context = keyboard->context;
	char text[64];

	if (sym == XKB_KEY_Multi_key &&
	    state == WL_KEYBOARD_KEY_STATE_RELEASED &&
	    keyboard->compose_state == state_normal) {
		keyboard->compose_state = state_compose;
		memset(&keyboard->compose_seq, 0, sizeof(struct compose_seq));
		return;
	}

	if (keyboard->compose_state == state_compose) {
		uint32_t i = 0;
		struct compose_seq *cs;

		if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
			return;

		for (i = 0; i < sizeof(ignore_keys_on_compose) / sizeof(ignore_keys_on_compose[0]); i++) {
			if (sym == ignore_keys_on_compose[i]) {
				input_method_context_key(context, display_get_serial(keyboard->display), time, key, state);
				return;
			}
		}

		for (i = 0; keyboard->compose_seq.keys[i] != 0; i++);

		keyboard->compose_seq.keys[i] = sym;

		cs = bsearch (&keyboard->compose_seq, compose_seqs,
			      sizeof(compose_seqs) / sizeof(compose_seqs[0]),
			      sizeof(compose_seqs[0]), compare_compose_keys);

		if (cs) {
			if (cs->keys[i + 1] == 0) {
				input_method_context_preedit_string(keyboard->context,
								    "", 0);
				input_method_context_commit_string(keyboard->context,
								   cs->text,
								   strlen(cs->text));
				keyboard->compose_state = state_normal;
			} else {
				uint32_t j = 0, idx = 0;

				for (; j <= i; j++) {
					idx += xkb_keysym_to_utf8(cs->keys[j], text + idx, sizeof(text) - idx);
				}

				input_method_context_preedit_string(keyboard->context,
								    text,
								    strlen(text));
			}
		} else {
			uint32_t j = 0, idx = 0;

			for (; j <= i; j++) {
				idx += xkb_keysym_to_utf8(keyboard->compose_seq.keys[j], text + idx, sizeof(text) - idx);
			}
			input_method_context_preedit_string(keyboard->context,
							    "", 0);
			input_method_context_commit_string(keyboard->context,
							   text,
							   strlen(text));
			keyboard->compose_state = state_normal;
		}
		return;
	}

	if (xkb_keysym_to_utf8(sym, text, sizeof(text)) <= 0) {
		input_method_context_key(context, display_get_serial(keyboard->display), time, key, state);
		return;
	}

	if (state == WL_KEYBOARD_KEY_STATE_PRESSED)
		return;

	input_method_context_commit_string(keyboard->context,
					   text,
					   strlen(text));
}

int
main(int argc, char *argv[])
{
	struct simple_im simple_im;

	memset(&simple_im, 0, sizeof(simple_im));

	simple_im.display = display_create(argc, argv);
	if (simple_im.display == NULL) {
		fprintf(stderr, "failed to create display: %m\n");
		return -1;
	}

	simple_im.context = NULL;
	simple_im.keyboard_input = keyboard_input_create(display_get_xkb_context(simple_im.display));
	keyboard_input_set_user_data(simple_im.keyboard_input, &simple_im);
	keyboard_input_set_key_handler(simple_im.keyboard_input, simple_im_key_handler);

	display_set_user_data(simple_im.display, &simple_im);
	display_set_global_handler(simple_im.display, global_handler);

	display_run(simple_im.display);

	return 0;
}