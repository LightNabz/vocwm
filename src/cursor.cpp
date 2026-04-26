#include "vocwm.h"

extern "C" {
    #include <wayland-server-core.h>
    #include <wlr/types/wlr_cursor.h>
    #include <wlr/types/wlr_pointer.h>
    #include <wlr/types/wlr_scene.h>
    #include <wlr/types/wlr_seat.h>
    #include <wlr/types/wlr_xcursor_manager.h>
}

// ============================================================
// Hit-test helpers
// ============================================================

// Walk the scene graph at (cx, cy) and return the VocwmToplevel
// that owns the node, or NULL if it's a layer surface / rect / etc.
static VocwmToplevel *toplevel_at(VocwmServer *server,
                                   double cx, double cy,
                                   double *sx, double *sy,
                                   struct wlr_surface **surface_out) {
    struct wlr_scene_node *node = wlr_scene_node_at(
        &server->scene->tree.node, cx, cy, sx, sy);

    if (!node || node->type != WLR_SCENE_NODE_BUFFER)
        return NULL;

    struct wlr_scene_buffer *sb = wlr_scene_buffer_from_node(node);
    struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(sb);
    if (!ss) return NULL;

    if (surface_out) *surface_out = ss->surface;

    // Walk up the scene tree to find the owning VocwmToplevel
    struct wlr_scene_tree *tree = node->parent;
    while (tree && tree != &server->scene->tree) {
        struct wlr_scene_node *n = &tree->node;
        if (n->data) return (VocwmToplevel *)n->data;
        tree = n->parent;
    }
    return NULL;
}

// ============================================================
// begin_interactive — enter move/resize grab mode
// ============================================================

void begin_interactive(struct VocwmToplevel *toplevel, enum VocwmCursorMode mode) {
    VocwmServer *server = toplevel->server;

    // only grab floating windows; tiling is handled by arrange_toplevels
    if (!toplevel->floating) return;

    server->cursor_mode      = mode;
    server->grabbed_toplevel = toplevel;
    server->grab_x           = server->cursor->x;
    server->grab_y           = server->cursor->y;
    server->grab_orig_x      = toplevel->x;
    server->grab_orig_y      = toplevel->y;

    const char *cursor_name = (mode == VOCWM_CURSOR_MOVE) ? "fleur" : "se-resize";
    wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, cursor_name);
}

// ============================================================
// process_cursor_motion — runs on every cursor movement
// ============================================================

void process_cursor_motion(struct VocwmServer *server, uint32_t time) {

    // --- grab mode: drag or resize ---
    if (server->cursor_mode == VOCWM_CURSOR_MOVE) {
        VocwmToplevel *t = server->grabbed_toplevel;
        if (!t) { server->cursor_mode = VOCWM_CURSOR_PASSTHROUGH; goto passthrough; }

        int new_x = server->grab_orig_x + (int)(server->cursor->x - server->grab_x);
        int new_y = server->grab_orig_y + (int)(server->cursor->y - server->grab_y);

        wlr_scene_node_set_position(&t->scene_tree->node, new_x, new_y);
        if (t->border_rect)
            wlr_scene_node_set_position(&t->border_rect->node,
                new_x - server->border_width,
                new_y - server->border_width);
        t->x = new_x;
        t->y = new_y;
        return;
    }

    if (server->cursor_mode == VOCWM_CURSOR_RESIZE) {
        // future: resize logic goes here
        return;
    }

passthrough:
    // --- passthrough: find surface under cursor ---
    {
        double sx, sy;
        struct wlr_surface *surface = NULL;
        VocwmToplevel *tl = toplevel_at(server,
            server->cursor->x, server->cursor->y,
            &sx, &sy, &surface);

        // Also check layer surfaces (waybar, etc.) — they live in a different
        // scene sub-tree so toplevel_at won't find them. Do a raw node walk.
        if (!surface) {
            struct wlr_scene_node *node = wlr_scene_node_at(
                &server->scene->tree.node,
                server->cursor->x, server->cursor->y,
                &sx, &sy);
            if (node && node->type == WLR_SCENE_NODE_BUFFER) {
                struct wlr_scene_buffer *sb = wlr_scene_buffer_from_node(node);
                struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(sb);
                if (ss) surface = ss->surface;
            }
            (void)tl;
        }

        if (surface) {
            wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
            wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
        } else {
            wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
            wlr_seat_pointer_clear_focus(server->seat);
        }
    }
}

// ============================================================
// Cursor event handlers
// ============================================================

static void server_cursor_motion(struct wl_listener *listener, void *data) {
    VocwmServer *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *ev = (struct wlr_pointer_motion_event *)data;
    wlr_cursor_move(server->cursor, &ev->pointer->base, ev->delta_x, ev->delta_y);
    process_cursor_motion(server, ev->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    VocwmServer *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *ev =
        (struct wlr_pointer_motion_absolute_event *)data;
    wlr_cursor_warp_absolute(server->cursor, &ev->pointer->base, ev->x, ev->y);
    process_cursor_motion(server, ev->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
    VocwmServer *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *ev = (struct wlr_pointer_button_event *)data;

    wlr_seat_pointer_notify_button(server->seat,
        ev->time_msec, ev->button, ev->state);

    if (ev->state == WL_POINTER_BUTTON_STATE_RELEASED) {
        // drop grab on any mouse button release
        if (server->cursor_mode != VOCWM_CURSOR_PASSTHROUGH) {
            server->cursor_mode      = VOCWM_CURSOR_PASSTHROUGH;
            server->grabbed_toplevel = NULL;
            wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
        }
        return;
    }

    // button press — focus whatever is under the cursor
    double sx, sy;
    struct wlr_surface *surface = NULL;
    VocwmToplevel *tl = toplevel_at(server,
        server->cursor->x, server->cursor->y,
        &sx, &sy, &surface);

    if (tl) focus_toplevel(tl);
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
    VocwmServer *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *ev = (struct wlr_pointer_axis_event *)data;
    wlr_seat_pointer_notify_axis(server->seat, ev->time_msec, ev->orientation,
        ev->delta, ev->delta_discrete, ev->source, ev->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
    VocwmServer *server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

// ============================================================
// Seat events
// ============================================================

void seat_request_cursor(struct wl_listener *listener, void *data) {
    VocwmServer *server = wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *ev =
        (struct wlr_seat_pointer_request_set_cursor_event *)data;
    // only honour cursor requests from the focused client
    if (server->seat->pointer_state.focused_client == ev->seat_client)
        wlr_cursor_set_surface(server->cursor, ev->surface,
            ev->hotspot_x, ev->hotspot_y);
}

void seat_request_set_selection(struct wl_listener *listener, void *data) {
    VocwmServer *server = wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *ev =
        (struct wlr_seat_request_set_selection_event *)data;
    wlr_seat_set_selection(server->seat, ev->source, ev->serial);
}

// ============================================================
// Setup
// ============================================================

void setup_cursor_listeners(struct VocwmServer *server) {
    server->cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);

    server->cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server->cursor->events.motion_absolute,
        &server->cursor_motion_absolute);

    server->cursor_button.notify = server_cursor_button;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);

    server->cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);

    server->cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);
}