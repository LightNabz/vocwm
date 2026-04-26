#include "vocwm.h"
#include <cassert>

extern "C" {
    #include <wayland-server-core.h>
    #include <wlr/types/wlr_xdg_shell.h>
    #include <wlr/types/wlr_scene.h>
    #include <wlr/types/wlr_seat.h>
}

// ============================================================
// focus_toplevel
// ============================================================

void focus_toplevel(struct VocwmToplevel *toplevel) {
    if (!toplevel) return;
    VocwmServer *server = toplevel->server;
    struct wlr_seat *seat = server->seat;
    struct wlr_surface *prev_surface = seat->keyboard_state.focused_surface;
    struct wlr_surface *surface = toplevel->xdg_toplevel->base->surface;

    if (prev_surface == surface) return;

    // deactivate previous toplevel
    if (prev_surface) {
        struct wlr_xdg_toplevel *prev =
            wlr_xdg_toplevel_try_from_wlr_surface(prev_surface);
        if (prev) wlr_xdg_toplevel_set_activated(prev, false);
    }

    // raise and re-insert at list head (MRU order)
    wlr_scene_node_raise_to_top(&toplevel->scene_tree->node);
    wl_list_remove(&toplevel->link);
    wl_list_insert(&server->toplevels, &toplevel->link);

    wlr_xdg_toplevel_set_activated(toplevel->xdg_toplevel, true);

    struct wlr_keyboard *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, surface,
            keyboard->keycodes, keyboard->num_keycodes,
            &keyboard->modifiers);
    }

    // update_borders AFTER seat state is updated — no race, focused_hint
    // is derived correctly because seat->keyboard_state.focused_surface
    // is now `surface`
    update_borders(server);
}

// ============================================================
// XDG Toplevel event handlers
// ============================================================

static void xdg_toplevel_map(struct wl_listener *listener, void *data) {
    VocwmToplevel *toplevel = wl_container_of(listener, toplevel, map);
    // insert before focus so arrange sees it in the list
    wl_list_insert(&toplevel->server->toplevels, &toplevel->link);
    focus_toplevel(toplevel);
    arrange_toplevels(toplevel->server);
}

static void xdg_toplevel_unmap(struct wl_listener *listener, void *data) {
    VocwmToplevel *toplevel = wl_container_of(listener, toplevel, unmap);

    // if we're dragging this toplevel, cancel the grab
    if (toplevel->server->grabbed_toplevel == toplevel) {
        toplevel->server->cursor_mode      = VOCWM_CURSOR_PASSTHROUGH;
        toplevel->server->grabbed_toplevel = NULL;
    }

    wl_list_remove(&toplevel->link);
    arrange_toplevels(toplevel->server);
}

static void xdg_toplevel_commit(struct wl_listener *listener, void *data) {
    VocwmToplevel *toplevel = wl_container_of(listener, toplevel, commit);
    if (toplevel->xdg_toplevel->base->initial_commit) {
        // let the client pick its initial size; we'll resize it on map
        wlr_xdg_toplevel_set_size(toplevel->xdg_toplevel, 0, 0);
    }
}

static void xdg_toplevel_destroy(struct wl_listener *listener, void *data) {
    VocwmToplevel *toplevel = wl_container_of(listener, toplevel, destroy);

    // clean up border rect if it exists
    if (toplevel->border_rect) {
        wlr_scene_node_destroy(&toplevel->border_rect->node);
        toplevel->border_rect = NULL;
    }

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

// Client requests interactive move (e.g. dragging title bar in a CSD window)
static void xdg_toplevel_request_move(struct wl_listener *listener, void *data) {
    VocwmToplevel *toplevel = wl_container_of(listener, toplevel, request_move);
    begin_interactive(toplevel, VOCWM_CURSOR_MOVE);
}

// Client requests resize — accepted but not yet fully implemented
static void xdg_toplevel_request_resize(struct wl_listener *listener, void *data) {
    VocwmToplevel *toplevel = wl_container_of(listener, toplevel, request_resize);
    begin_interactive(toplevel, VOCWM_CURSOR_RESIZE);
}

static void xdg_toplevel_request_maximize(struct wl_listener *listener, void *data) {
    VocwmToplevel *toplevel = wl_container_of(listener, toplevel, request_maximize);
    if (toplevel->xdg_toplevel->base->initialized)
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}

static void xdg_toplevel_request_fullscreen(struct wl_listener *listener, void *data) {
    VocwmToplevel *toplevel = wl_container_of(listener, toplevel, request_fullscreen);
    if (toplevel->xdg_toplevel->base->initialized)
        wlr_xdg_surface_schedule_configure(toplevel->xdg_toplevel->base);
}

// ============================================================
// server_new_xdg_toplevel
// ============================================================

void server_new_xdg_toplevel(struct wl_listener *listener, void *data) {
    VocwmServer *server = wl_container_of(listener, server, new_xdg_toplevel);
    struct wlr_xdg_toplevel *xdg_toplevel = (struct wlr_xdg_toplevel *)data;

    VocwmToplevel *toplevel = (VocwmToplevel *)calloc(1, sizeof(*toplevel));
    toplevel->server        = server;
    toplevel->xdg_toplevel  = xdg_toplevel;
    toplevel->floating      = false;
    toplevel->border_rect   = NULL;

    // create scene tree inside the tiling layer
    toplevel->scene_tree =
        wlr_scene_xdg_surface_create(server->tiling_tree, xdg_toplevel->base);
    toplevel->scene_tree->node.data = toplevel;
    xdg_toplevel->base->data        = toplevel->scene_tree;

#define LISTEN(signal, member, cb) \
    toplevel->member.notify = cb; \
    wl_signal_add(&signal, &toplevel->member)

    LISTEN(xdg_toplevel->base->surface->events.map,       map,               xdg_toplevel_map);
    LISTEN(xdg_toplevel->base->surface->events.unmap,     unmap,             xdg_toplevel_unmap);
    LISTEN(xdg_toplevel->base->surface->events.commit,    commit,            xdg_toplevel_commit);
    LISTEN(xdg_toplevel->events.destroy,                  destroy,           xdg_toplevel_destroy);
    LISTEN(xdg_toplevel->events.request_move,             request_move,      xdg_toplevel_request_move);
    LISTEN(xdg_toplevel->events.request_resize,           request_resize,    xdg_toplevel_request_resize);
    LISTEN(xdg_toplevel->events.request_maximize,         request_maximize,  xdg_toplevel_request_maximize);
    LISTEN(xdg_toplevel->events.request_fullscreen,       request_fullscreen,xdg_toplevel_request_fullscreen);

#undef LISTEN
}

// ============================================================
// XDG Popup
// ============================================================

static void xdg_popup_commit(struct wl_listener *listener, void *data) {
    VocwmPopup *popup = wl_container_of(listener, popup, commit);
    if (popup->xdg_popup->base->initial_commit)
        wlr_xdg_surface_schedule_configure(popup->xdg_popup->base);
}

static void xdg_popup_destroy(struct wl_listener *listener, void *data) {
    VocwmPopup *popup = wl_container_of(listener, popup, destroy);
    wl_list_remove(&popup->commit.link);
    wl_list_remove(&popup->destroy.link);
    free(popup);
}

void server_new_xdg_popup(struct wl_listener *listener, void *data) {
    struct wlr_xdg_popup *xdg_popup = (struct wlr_xdg_popup *)data;
    VocwmPopup *popup = (VocwmPopup *)calloc(1, sizeof(*popup));
    popup->xdg_popup = xdg_popup;

    struct wlr_xdg_surface *parent =
        wlr_xdg_surface_try_from_wlr_surface(xdg_popup->parent);
    assert(parent != NULL);

    struct wlr_scene_tree *parent_tree = (struct wlr_scene_tree *)parent->data;
    xdg_popup->base->data =
        wlr_scene_xdg_surface_create(parent_tree, xdg_popup->base);

    popup->commit.notify = xdg_popup_commit;
    wl_signal_add(&xdg_popup->base->surface->events.commit, &popup->commit);

    popup->destroy.notify = xdg_popup_destroy;
    wl_signal_add(&xdg_popup->events.destroy, &popup->destroy);
}