#include "../include/vocwm.h"

extern "C" {
    #include <wayland-server-core.h>
    #include <wlr/types/wlr_input_device.h>
    #include <wlr/types/wlr_keyboard.h>
    #include <wlr/types/wlr_seat.h>
    #include <xkbcommon/xkbcommon.h>
}

static void keyboard_handle_modifiers(struct wl_listener *listener, void *data) {
    struct VocwmKeyboard *keyboard = wl_container_of(listener, keyboard, modifiers);
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_modifiers(keyboard->server->seat, &keyboard->wlr_keyboard->modifiers);
}

static void keyboard_handle_key(struct wl_listener *listener, void *data) {
    struct VocwmKeyboard *keyboard = wl_container_of(listener, keyboard, key);
    struct wlr_keyboard_key_event *event = (struct wlr_keyboard_key_event *)data;
    wlr_seat_set_keyboard(keyboard->server->seat, keyboard->wlr_keyboard);
    wlr_seat_keyboard_notify_key(keyboard->server->seat,
        event->time_msec, event->keycode, event->state);
}

static void keyboard_handle_destroy(struct wl_listener *listener, void *data) {
    struct VocwmKeyboard *keyboard = wl_container_of(listener, keyboard, destroy);
    wl_list_remove(&keyboard->modifiers.link);
    wl_list_remove(&keyboard->key.link);
    wl_list_remove(&keyboard->destroy.link);
    wl_list_remove(&keyboard->link);
    free(keyboard);
}

void server_new_keyboard(struct VocwmServer *server, struct wlr_input_device *device) {
    struct wlr_keyboard *wlr_keyboard = wlr_keyboard_from_input_device(device);
    struct VocwmKeyboard *keyboard = (struct VocwmKeyboard *)calloc(1, sizeof(*keyboard));
    keyboard->server = server;
    keyboard->wlr_keyboard = wlr_keyboard;
    struct xkb_context *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    struct xkb_keymap *keymap = xkb_keymap_new_from_names(context, NULL, XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(wlr_keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
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
    if (!wl_list_empty(&server->keyboards)) caps |= WL_SEAT_CAPABILITY_KEYBOARD;
    wlr_seat_set_capabilities(server->seat, caps);
}
