#include "vocwm.h"
#include <stdlib.h>

extern "C" {
    #include <wayland-server-core.h>
    #include <wlr/types/wlr_layer_shell_v1.h>
    #include <wlr/types/wlr_scene.h>
    #include <wlr/types/wlr_output.h>
    #include <wlr/types/wlr_output_layout.h>
    #include <wlr/util/log.h>

// We wrap the entire implementation block in extern "C" so the linker
// can find these functions from main.cpp without C++ name mangling.

void arrange_layers(VocwmServer *server) {
    if (wl_list_empty(&server->outputs)) return;

    // Single-monitor logic: first output
    VocwmOutput *output = wl_container_of(server->outputs.next, output, link);
    int ow, oh;
    wlr_output_effective_resolution(output->wlr_output, &ow, &oh);

    int top = 0, bottom = 0, left = 0, right = 0;

    VocwmLayerSurface *ls;
    wl_list_for_each(ls, &server->layer_surfaces, link) {
        struct wlr_layer_surface_v1 *wls = ls->wlr_layer_surface;
        
        // FIX: In wlroots 0.19, mapped is moved to the underlying surface
        if (!wls->surface->mapped) continue;

        int32_t excl = wls->current.exclusive_zone;
        if (excl <= 0) continue;

        uint32_t anchor = wls->current.anchor;
        
        bool anchored_top    = (anchor & WLR_EDGE_TOP)    != 0;
        bool anchored_bottom = (anchor & WLR_EDGE_BOTTOM) != 0;
        bool anchored_left   = (anchor & WLR_EDGE_LEFT)   != 0;
        bool anchored_right  = (anchor & WLR_EDGE_RIGHT)  != 0;

        if (anchored_top && !anchored_bottom)    top    += excl;
        else if (anchored_bottom && !anchored_top) bottom += excl;
        else if (anchored_left && !anchored_right) left   += excl;
        else if (anchored_right && !anchored_left) right  += excl;
    }

    server->usable_x = left;
    server->usable_y = top;
    server->usable_w = ow - left - right;
    server->usable_h = oh - top - bottom;

    arrange_toplevels(server);
}

static struct wlr_scene_tree *scene_tree_for_layer(VocwmServer *server,
                                                    enum zwlr_layer_shell_v1_layer layer) {
    switch (layer) {
        case ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND: return server->layer_bg;
        case ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM:     return server->layer_bottom;
        case ZWLR_LAYER_SHELL_V1_LAYER_TOP:        return server->layer_top;
        case ZWLR_LAYER_SHELL_V1_LAYER_OVERLAY:    return server->layer_overlay;
        default:                                    return server->layer_top;
    }
}

static void layer_surface_map(struct wl_listener *listener, void *data) {
    VocwmLayerSurface *ls = wl_container_of(listener, ls, map);
    arrange_layers(ls->server);
}

static void layer_surface_unmap(struct wl_listener *listener, void *data) {
    VocwmLayerSurface *ls = wl_container_of(listener, ls, unmap);
    arrange_layers(ls->server);
}

static void layer_surface_commit(struct wl_listener *listener, void *data) {
    VocwmLayerSurface *ls = wl_container_of(listener, ls, commit);
    struct wlr_layer_surface_v1 *wls = ls->wlr_layer_surface;

    if (!wls->initialized) return;

    VocwmOutput *output = wl_container_of(ls->server->outputs.next, output, link);
    int ow, oh;
    wlr_output_effective_resolution(output->wlr_output, &ow, &oh);

    struct wlr_box full = { 0, 0, ow, oh };
    wlr_scene_layer_surface_v1_configure(ls->scene_layer, &full, &full);

    arrange_layers(ls->server);
}

static void layer_surface_destroy(struct wl_listener *listener, void *data) {
    VocwmLayerSurface *ls = wl_container_of(listener, ls, destroy);
    wl_list_remove(&ls->map.link);
    wl_list_remove(&ls->unmap.link);
    wl_list_remove(&ls->commit.link);
    wl_list_remove(&ls->destroy.link);
    wl_list_remove(&ls->link);
    free(ls);
}

void server_new_layer_surface(struct wl_listener *listener, void *data) {
    VocwmServer *server = wl_container_of(listener, server, new_layer_surface);
    struct wlr_layer_surface_v1 *wls = (struct wlr_layer_surface_v1 *)data;

    if (!wls->output && !wl_list_empty(&server->outputs)) {
        VocwmOutput *out = wl_container_of(server->outputs.next, out, link);
        wls->output = out->wlr_output;
    }

    if (!wls->output) {
        wlr_layer_surface_v1_destroy(wls);
        return;
    }

    VocwmLayerSurface *ls = (VocwmLayerSurface *)calloc(1, sizeof(*ls));
    ls->server = server;
    ls->wlr_layer_surface = wls;

    struct wlr_scene_tree *tree = scene_tree_for_layer(server, wls->current.layer);
    ls->scene_layer = wlr_scene_layer_surface_v1_create(tree, wls);
    if (!ls->scene_layer) {
        free(ls);
        return;
    }

    ls->map.notify = layer_surface_map;
    wl_signal_add(&wls->surface->events.map, &ls->map);

    ls->unmap.notify = layer_surface_unmap;
    wl_signal_add(&wls->surface->events.unmap, &ls->unmap);

    ls->commit.notify = layer_surface_commit;
    wl_signal_add(&wls->surface->events.commit, &ls->commit);

    ls->destroy.notify = layer_surface_destroy;
    wl_signal_add(&wls->events.destroy, &ls->destroy);

    wl_list_insert(&server->layer_surfaces, &ls->link);

    // FIX: Using _namespace because of the #define in vocwm.h
    wlr_log(WLR_DEBUG, "new layer surface: namespace=%s layer=%d",
        wls->_namespace ? wls->_namespace : "(null)", wls->current.layer);
}

}