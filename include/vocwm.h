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
    #define namespace _namespace
    #include <wlr/types/wlr_layer_shell_v1.h>
    #undef namespace
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
    #include <lua.h>
}

struct wlr_xdg_shell;
struct wlr_xdg_toplevel;
struct wlr_xdg_popup;

// ============================================================
// Cursor grab modes
// ============================================================

enum VocwmCursorMode {
    VOCWM_CURSOR_PASSTHROUGH = 0,
    VOCWM_CURSOR_MOVE,
    VOCWM_CURSOR_RESIZE,
};

// ============================================================
// Main server struct
// ============================================================

struct VocwmServer {
    struct wl_display *display;
    struct wlr_backend *backend;
    struct wlr_renderer *renderer;
    struct wlr_allocator *allocator;
    struct wlr_scene *scene;
    struct wlr_scene_output_layout *scene_layout;

    // Scene layers bottom→top: bg, bottom, [toplevels], top, overlay
    struct wlr_scene_tree *layer_bg;
    struct wlr_scene_tree *layer_bottom;
    struct wlr_scene_tree *tiling_tree;
    struct wlr_scene_tree *layer_top;
    struct wlr_scene_tree *layer_overlay;

    // XDG shell
    struct wlr_xdg_shell *xdg_shell;
    struct wl_listener new_xdg_toplevel;
    struct wl_listener new_xdg_popup;
    struct wl_list toplevels;

    // Layer shell (swaybg, waybar, etc.)
    struct wlr_layer_shell_v1 *layer_shell;
    struct wl_listener new_layer_surface;
    struct wl_list layer_surfaces;

    // Cursor
    struct wlr_cursor *cursor;
    struct wlr_xcursor_manager *cursor_mgr;
    struct wl_listener cursor_motion;
    struct wl_listener cursor_motion_absolute;
    struct wl_listener cursor_button;
    struct wl_listener cursor_axis;
    struct wl_listener cursor_frame;

    // Cursor grab state
    enum VocwmCursorMode cursor_mode;
    struct VocwmToplevel *grabbed_toplevel;
    double grab_x, grab_y;
    int grab_orig_x, grab_orig_y;

    // Seat & input
    struct wlr_seat *seat;
    struct wl_listener new_input;
    struct wl_listener request_cursor;
    struct wl_listener request_set_selection;
    struct wl_list keyboards;

    // Output
    struct wlr_output_layout *output_layout;
    struct wl_list outputs;
    struct wl_listener new_output;

    // Lua
    lua_State *lua;

    // Tiling config — all set from Lua (default.lua), never hardcoded in C
    float master_ratio;
    int outer_gap;
    int inner_gap;
    int border_width;
    float border_color[4];
    float border_color_focused[4];

    // Usable screen area after layer shell reservations
    int usable_x, usable_y, usable_w, usable_h;
};

// ============================================================
// Output
// ============================================================

struct VocwmOutput {
    struct wl_list link;
    struct VocwmServer *server;
    struct wlr_output *wlr_output;
    struct wl_listener frame;
    struct wl_listener request_state;
    struct wl_listener destroy;
};

// ============================================================
// XDG Toplevel
// ============================================================

struct VocwmToplevel {
    struct wl_list link;
    struct VocwmServer *server;
    struct wlr_xdg_toplevel *xdg_toplevel;
    struct wlr_scene_tree *scene_tree;
    struct wlr_scene_rect *border_rect;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
    struct wl_listener request_move;
    struct wl_listener request_resize;
    struct wl_listener request_maximize;
    struct wl_listener request_fullscreen;

    bool floating;
    int x, y;
    int width, height;
};

// ============================================================
// Layer surface
// ============================================================

struct VocwmLayerSurface {
    struct wl_list link;
    struct VocwmServer *server;
    struct wlr_layer_surface_v1 *wlr_layer_surface;
    struct wlr_scene_layer_surface_v1 *scene_layer;

    struct wl_listener map;
    struct wl_listener unmap;
    struct wl_listener commit;
    struct wl_listener destroy;
};

// ============================================================
// Keyboard
// ============================================================

struct VocwmKeyboard {
    struct wl_list link;
    struct VocwmServer *server;
    struct wlr_keyboard *wlr_keyboard;
    struct wl_listener modifiers;
    struct wl_listener key;
    struct wl_listener destroy;
};

// ============================================================
// XDG Popup
// ============================================================

struct VocwmPopup {
    struct wlr_xdg_popup *xdg_popup;
    struct wl_listener commit;
    struct wl_listener destroy;
};

// ============================================================
// Function declarations
// ============================================================

// tiling.cpp
void lua_init(VocwmServer *server);
void arrange_toplevels(VocwmServer *server);
void update_borders(VocwmServer *server);

// xdg_surface.cpp
void focus_toplevel(struct VocwmToplevel *toplevel);
void server_new_xdg_toplevel(struct wl_listener *listener, void *data);
void server_new_xdg_popup(struct wl_listener *listener, void *data);

// layer_shell.cpp, needs to be wrapped in extern "C" because of the wl_listener callback signatures
extern "C" {
    void server_new_layer_surface(struct wl_listener *listener, void *data);
    void arrange_layers(VocwmServer *server);
}

// keyboard.cpp
void server_new_keyboard(struct VocwmServer *server, struct wlr_input_device *device);
void server_new_input(struct wl_listener *listener, void *data);

// cursor.cpp
void process_cursor_motion(struct VocwmServer *server, uint32_t time);
void seat_request_cursor(struct wl_listener *listener, void *data);
void seat_request_set_selection(struct wl_listener *listener, void *data);
void setup_cursor_listeners(struct VocwmServer *server);
void begin_interactive(struct VocwmToplevel *toplevel, enum VocwmCursorMode mode);

// output.cpp
void server_new_output(struct wl_listener *listener, void *data);