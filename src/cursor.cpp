#include "../include/vocwm.h"

extern "C" {
    #include <wayland-server-core.h>
    #include <wlr/types/wlr_cursor.h>
    #include <wlr/types/wlr_pointer.h>
    #include <wlr/types/wlr_scene.h>
    #include <wlr/types/wlr_seat.h>
    #include <wlr/types/wlr_xcursor_manager.h>
}

void process_cursor_motion(struct VocwmServer *server, uint32_t time) {
    double sx, sy;
    struct wlr_surface *surface = NULL;
    struct wlr_scene_node *node = wlr_scene_node_at(
        &server->scene->tree.node, server->cursor->x, server->cursor->y, &sx, &sy);
    if (node && node->type == WLR_SCENE_NODE_BUFFER) {
        struct wlr_scene_buffer *sb = wlr_scene_buffer_from_node(node);
        struct wlr_scene_surface *ss = wlr_scene_surface_try_from_buffer(sb);
        if (ss) surface = ss->surface;
    }
    if (surface) {
        wlr_seat_pointer_notify_enter(server->seat, surface, sx, sy);
        wlr_seat_pointer_notify_motion(server->seat, time, sx, sy);
    } else {
        wlr_cursor_set_xcursor(server->cursor, server->cursor_mgr, "default");
        wlr_seat_pointer_clear_focus(server->seat);
    }
}

static void server_cursor_motion(struct wl_listener *listener, void *data) {
    struct VocwmServer *server = wl_container_of(listener, server, cursor_motion);
    struct wlr_pointer_motion_event *event = (struct wlr_pointer_motion_event *)data;
    wlr_cursor_move(server->cursor, &event->pointer->base, event->delta_x, event->delta_y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_motion_absolute(struct wl_listener *listener, void *data) {
    struct VocwmServer *server = wl_container_of(listener, server, cursor_motion_absolute);
    struct wlr_pointer_motion_absolute_event *event = (struct wlr_pointer_motion_absolute_event *)data;
    wlr_cursor_warp_absolute(server->cursor, &event->pointer->base, event->x, event->y);
    process_cursor_motion(server, event->time_msec);
}

static void server_cursor_button(struct wl_listener *listener, void *data) {
    struct VocwmServer *server = wl_container_of(listener, server, cursor_button);
    struct wlr_pointer_button_event *event = (struct wlr_pointer_button_event *)data;
    wlr_seat_pointer_notify_button(server->seat, event->time_msec, event->button, event->state);
}

static void server_cursor_axis(struct wl_listener *listener, void *data) {
    struct VocwmServer *server = wl_container_of(listener, server, cursor_axis);
    struct wlr_pointer_axis_event *event = (struct wlr_pointer_axis_event *)data;
    wlr_seat_pointer_notify_axis(server->seat, event->time_msec, event->orientation,
        event->delta, event->delta_discrete, event->source, event->relative_direction);
}

static void server_cursor_frame(struct wl_listener *listener, void *data) {
    struct VocwmServer *server = wl_container_of(listener, server, cursor_frame);
    wlr_seat_pointer_notify_frame(server->seat);
}

void seat_request_cursor(struct wl_listener *listener, void *data) {
    struct VocwmServer *server = wl_container_of(listener, server, request_cursor);
    struct wlr_seat_pointer_request_set_cursor_event *event =
        (struct wlr_seat_pointer_request_set_cursor_event *)data;
    if (server->seat->pointer_state.focused_client == event->seat_client)
        wlr_cursor_set_surface(server->cursor, event->surface, event->hotspot_x, event->hotspot_y);
}

void seat_request_set_selection(struct wl_listener *listener, void *data) {
    struct VocwmServer *server = wl_container_of(listener, server, request_set_selection);
    struct wlr_seat_request_set_selection_event *event =
        (struct wlr_seat_request_set_selection_event *)data;
    wlr_seat_set_selection(server->seat, event->source, event->serial);
}

// Exported functions to set up cursor listeners
void setup_cursor_listeners(struct VocwmServer *server) {
    server->cursor_motion.notify = server_cursor_motion;
    wl_signal_add(&server->cursor->events.motion, &server->cursor_motion);
    server->cursor_motion_absolute.notify = server_cursor_motion_absolute;
    wl_signal_add(&server->cursor->events.motion_absolute, &server->cursor_motion_absolute);
    server->cursor_button.notify = server_cursor_button;
    wl_signal_add(&server->cursor->events.button, &server->cursor_button);
    server->cursor_axis.notify = server_cursor_axis;
    wl_signal_add(&server->cursor->events.axis, &server->cursor_axis);
    server->cursor_frame.notify = server_cursor_frame;
    wl_signal_add(&server->cursor->events.frame, &server->cursor_frame);
}
