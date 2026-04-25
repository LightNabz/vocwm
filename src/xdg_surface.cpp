#include "../include/vocwm.h"
#include <cassert>

extern "C" {
    #include <wayland-server-core.h>
    #include <wlr/types/wlr_xdg_shell.h>
    #include <wlr/types/wlr_scene.h>
    #include <wlr/types/wlr_seat.h>
}

void focus_toplevel(struct VocwmToplevel *toplevel) {
    if (toplevel == NULL) return;
    struct VocwmServer *server = toplevel->server;
    struct wlr_seat *seat = server->seat;
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;
    if (prev_surface == surface) return;
    if (prev_surface) {
        struct wlr_xdg_toplevel *prev =
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev) wlr_xdg_toplevel_set_activated(prev, false);
    }
    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    wl_list_remove(&toplevel->link);
    wl_list_insert(&server->toplevels, &toplevel->link);
    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface,
            keyboard->keycodes, keyboard->num_keycodes, &keyboard->modifiers);
    }
}

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    struct VocwmToplevel *toplevel = wl_container_of(listener, toplevel, map);
    wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
    focus_toplevel(toplevel);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    struct VocwmToplevel *toplevel = wl_container_of(listener, toplevel, unmap);
    wl_list_remove(&toplevel->link);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
    struct VocwmToplevel *toplevel = wl_container_of(listener, toplevel, commit);
    if (toplevel->xdg_toplevel->base->initial_commit) {
        // let client pick its own size
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
    }
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    struct VocwmToplevel *toplevel = wl_container_of(listener, toplevel, destroy);
    wl_list_remove(&toplevel->map.link);
    wl_list_remove(&toplevel->unmap.link);
    wl_list_remove(&toplevel->commit.link);
    wl_list_remove(&toplevel->destroy.link);
    wl_list_remove(&toplevel->request_move.link);
    wl_list_remove(&toplevel->request_resize.link);
    wl_list_remove(&toplevel->request_maximize.link);
    wl_list_remove(&toplevel->request_fullscreen.link);
    free(toplevel);
}

static void xdg_toplevel_request_move(struct wl_listener *listener, void *data) {}
static void xdg_toplevel_request_resize(struct wl_listener *listener, void *data) {}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data) {
    struct VocwmToplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
    if (toplevel->xdg_toplevel->base->initialized)
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
    struct VocwmToplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
    if (toplevel->xdg_toplevel->base->initialized)
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}

void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    struct VocwmServer *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = (struct wlr_xdg_toplevel *)data;
    struct VocwmToplevel *toplevel = (struct VocwmToplevel *)calloc(1, sizeof(*toplevel));
    toplevel->server = server;
    toplevel->xdg_toplevel = xdg_toplevel;
    toplevel->scene_tree =
        wlr_scene_xdg_surface_create(&server->scene->tree, xdg_toplevel->base);
    toplevel->scene_tree->node.data = toplevel;
    xdg_toplevel->base->data = toplevel->scene_tree;
    toplevel->map.notify = xdg_toplevel_map;
    wl_signal_add(&xdg_toplevel->base->surface->events.map, &toplevel->map);
    toplevel->unmap.notify = xdg_toplevel_unmap;
    wl_signal_add(&xdg_toplevel->base->surface->events.unmap, &toplevel->unmap);
    toplevel->commit.notify = xdg_toplevel_commit;
    wl_signal_add(&xdg_toplevel->base->surface->events.commit, &toplevel->commit);
    toplevel->destroy.notify = xdg_toplevel_destroy;
    wl_signal_add(&xdg_toplevel->events.destroy, &toplevel->destroy);
    toplevel->request_move.notify = xdg_toplevel_request_move;
    wl_signal_add(&xdg_toplevel->events.request_move, &toplevel->request_move);
    toplevel->request_resize.notify = xdg_toplevel_request_resize;
    wl_signal_add(&xdg_toplevel->events.request_resize, &toplevel->request_resize);
    toplevel->request_maximize.notify = xdg_toplevel_request_maximize;
    wl_signal_add(&xdg_toplevel->events.request_maximize, &toplevel->request_maximize);
    toplevel->request_fullscreen.notify = xdg_toplevel_request_fullscreen;
    wl_signal_add(&xdg_toplevel->events.request_fullscreen, &toplevel->request_fullscreen);
}

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
    struct VocwmPopup *popup = wl_container_of(listener, popup, commit);
    if (popup->xdg_popup->base->initial_commit)
        wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
    struct VocwmPopup *popup = wl_container_of(listener, popup, destroy);
    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);
    free(popup);
}

void server_new_xdg_popup(struct wl_listener *listener, void *data) {
    struct wlr_xdg_popup *xdg_popup = (struct wlr_xdg_popup *)data;
    struct VocwmPopup *popup = (struct VocwmPopup *)calloc(1, sizeof(*popup));
    popup->xdg_popup = xdg_popup;
    struct wlr_xdg_surface *parent =
        wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    assert(parent != NULL);
    struct wlr_scene_tree *parent_tree = (struct wlr_scene_tree *)parent->data;
    xdg_popup->base->data = wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);
    popup->commit.notify = xdg_popup_commit;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);
    popup->destroy.notify = xdg_popup_destroy;
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}
