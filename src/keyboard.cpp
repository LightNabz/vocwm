#include "vocwm.h"

extern "C" {
    #include <wayland-server-core.h>
    #include <wlr/types/wlr_input_device.h>
    #include <wlr/types/wlr_keyboard.h>
    #include <wlr/types/wlr_seat.h>
    #include <xkbcommon/xkbcommon.h>
}

// Declared in tiling.cpp
bool lua_dispatch_binding(VocwmServer *server, const char *combo);

// ============================================================
// Key combo string builder
//
// Converts a modifier bitmask + keysym into a human-readable
// string matching what the user writes in bind():
//   "Super+Return", "Super+Shift+t", "Super+h", etc.
// ============================================================

static void build_combo(uint32_t mods, xkb_keysym_t sym,
                        char *out, size_t outsz) {
    out[0] = '\0';

    if (mods & WLR_MODIFIER_LOGO)  strncat(out, "Super+",  outsz - strlen(out) - 1);
    if (mods & WLR_MODIFIER_CTRL)  strncat(out, "Ctrl+",   outsz - strlen(out) - 1);
    if (mods & WLR_MODIFIER_ALT)   strncat(out, "Alt+",    outsz - strlen(out) - 1);
    if (mods & WLR_MODIFIER_SHIFT) strncat(out, "Shift+",  outsz - strlen(out) - 1);

    char sym_name[64];
    xkb_keysym_get_name(sym, sym_name, sizeof(sym_name));
    strncat(out, sym_name, outsz - strlen(out) - 1);
}

// ============================================================
// Keyboard event handlers
// ============================================================

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    struct VocwmKeyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat,
        &keyboard->wlr_keyboard->modifiers);
}

static bool keyboard_handle_key_press(struct VocwmKeyboard *keyboard,
                                      struct wlr_keyboard_key_event *event) {
    struct wlr_keyboard *wlr_kb = keyboard->wlr_keyboard;
    uint32_t mods = wlr_keyboard_get_modifiers(wlr_kb);

    // gate: need at least one WM-relevant modifier
    if (!(mods & (WLR_MODIFIER_LOGO | WLR_MODIFIER_ALT | WLR_MODIFIER_CTRL)))
        return false;

    uint32_t keycode = event->keycode + 8;
    const xkb_keysym_t *syms;
    int nsyms = xkb_state_key_get_syms(wlr_kb->xkb_state, keycode, &syms);

    for (int i = 0; i < nsyms; i++) {
        // normalize to unshifted keysym so "Alt+Shift+h" not "Alt+Shift+H"
        xkb_keysym_t sym = xkb_keysym_to_lower(syms[i]);
        char combo[128];
        build_combo(mods, sym, combo, sizeof(combo));
        wlr_log(WLR_DEBUG, "keybind attempt: [%s]", combo); // add this temporarily
        if (lua_dispatch_binding(keyboard->server, combo))
            return true;
    }
    return false;
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct VocwmKeyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard_key_event *event = (struct wlr_keyboard_key_event *)data;

    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);

    // only try to intercept key-press events, not releases
    bool consumed = false;
    if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED)
        consumed = keyboard_handle_key_press(keyboard, event);

    // if not consumed by a WM binding, forward to the focused client
    if (!consumed) {
        wlr_seat_keyboard_notify_key(keyboard->server->seat,
            event->time_msec, event->keycode, event->state);
    }
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    struct VocwmKeyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

// ============================================================
// Public: new keyboard / new input
// ============================================================

void server_new_keyboard(struct VocwmServer *server, struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);
    struct VocwmKeyboard *keyboard = (struct VocwmKeyboard *)calloc(1, sizeof(*keyboard));
    keyboard->server = server;
    keyboard->wlr_keyboard = wlr_keyboard;

    struct xkb_context *ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(
        ctx, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(ctx);

    wlr_keyboard_set_repeat_info(wlr_keyboard, 25, 600);

    keyboard->modifiers.notify = keyboard_handle_modifiers;
    wl_signal_add(&wlr_keyboard->events.modifiers, &keyboard->modifiers);

    keyboard->key.notify = keyboard_handle_key;
    wl_signal_add(&wlr_keyboard->events.key, &keyboard->key);

    keyboard->destroy.notify = keyboard_handle_destroy;
    wl_signal_add(&device->events.destroy, &keyboard->destroy);

    wlr_seat_set_keyboard(server->seat, wlr_keyboard);
    wl_list_insert(&server->keyboards, &keyboard->link);
}

void server_new_input(struct wl_listener *listener, void *data) {
    struct VocwmServer *server = wl_container_of(listener, server, new_input);
    struct wlr_input_device *device = (struct wlr_input_device *)data;

    if (device->type == WLR_INPUT_DEVICE_KEYBOARD)
        server_new_keyboard(server, device);
    else if (device->type == WLR_INPUT_DEVICE_POINTER)
        wlr_cursor_attach_input_device(server->cursor, device);

    uint32_t caps = WL_SEAT_CAPABILITY_POINTER;
    if (!wl_list_empty(&server->keyboards))
        caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(server->seat, caps);
}