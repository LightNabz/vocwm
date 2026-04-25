#pragma once

extern "C" {
    #include <wayland-server-core.h>
    #include <wlr/backend.h>
    #include <wlr/render/allocator.h>
    #include <wlr/render/wlr_renderer.h>
    #include <wlr/types/wlr_compositor.h>
    #include <wlr/types/wlr_cursor.h>
    #include <wlr/types/wlr_data_device.h>
    #include <wlr/types/wlr_input_device.h>
    #include <wlr/types/wlr_keyboard.h>
    #include <wlr/types/wlr_output.h>
    #include <wlr/types/wlr_output_layout.h>
    #include <wlr/types/wlr_pointer.h>
    #define static
    #include <wlr/types/wlr_scene.h>
    #undef static
    #include <wlr/types/wlr_seat.h>
    #include <wlr/types/wlr_subcompositor.h>
    #include <wlr/types/wlr_xcursor_manager.h>
    #include <wlr/util/log.h>
}

// Forward declare xdg_shell type to avoid including problematic header
struct wlr_xdg_shell;
struct wlr_xdg_toplevel;
struct wlr_xdg_popup;

struct VocwmServer {
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_list toplevels;

    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    struct wlr_seat *seat;
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
    struct wl_list keyboards;

    struct wlr_output_layout *output_layout;
    struct wl_list outputs;
    struct wl_listener new_output;
};

struct VocwmOutput {
    struct wl_list link;
    struct VocwmServer *server;
    struct wlr_output *wlr_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

struct VocwmToplevel {
    struct wl_list link;
    struct VocwmServer *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;
    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;
};

struct VocwmKeyboard {
    struct wl_list link;
    struct VocwmServer *server;
    struct wlr_keyboard *wlr_keyboard;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

struct VocwmPopup {
    struct wlr_xdg_popup *xdg_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

// Toplevel functions
void focus_toplevel(struct VocwmToplevel *toplevel);
void server_new_xdg_toplevel(struct wl_listener *listener, void *data);
void server_new_xdg_popup(struct wl_listener *listener, void *data);

// Keyboard functions
void server_new_keyboard(struct VocwmServer *server, struct wlr_input_device *device);
void server_new_input(struct wl_listener *listener, void *data);

// Cursor functions
void process_cursor_motion(struct VocwmServer *server, uint32_t time);
void seat_request_cursor(struct wl_listener *listener, void *data);
void seat_request_set_selection(struct wl_listener *listener, void *data);

// Output functions
void server_new_output(struct wl_listener *listener, void *data);
