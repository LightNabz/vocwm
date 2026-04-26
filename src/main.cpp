#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include "vocwm.h"

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
    #include <wlr/types/wlr_layer_shell_v1.h>
    #include <wlr/types/wlr_output.h>
    #include <wlr/types/wlr_output_layout.h>
    #include <wlr/types/wlr_pointer.h>
    #define static
    #include <wlr/types/wlr_scene.h>
    #undef static
    #include <wlr/types/wlr_seat.h>
    #include <wlr/types/wlr_subcompositor.h>
    #include <wlr/types/wlr_xcursor_manager.h>
    #include <wlr/types/wlr_xdg_shell.h>
    #include <wlr/util/log.h>
}

int main(int argc, char *argv[]) {
    wlr_log_init(WLR_DEBUG, NULL);

    struct VocwmServer server = {};
    server.display = wl_display_create();

    // Backend (DRM/KMS or headless/X11 in nested mode)
    server.backend = wlr_backend_autocreate(
        wl_display_get_event_loop(server.display), NULL);
    if (!server.backend) {
        wlr_log(WLR_ERROR, "Failed to create backend");
        return 1;
    }

    server.renderer = wlr_renderer_autocreate(server.backend);
    if (!server.renderer) {
        wlr_log(WLR_ERROR, "Failed to create renderer");
        return 1;
    }
    wlr_renderer_init_wl_display(server.renderer, server.display);

    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if (!server.allocator) {
        wlr_log(WLR_ERROR, "Failed to create allocator");
        return 1;
    }

    // Core Wayland protocols
    wlr_compositor_create(server.display, 5, server.renderer);
    wlr_subcompositor_create(server.display);
    wlr_data_device_manager_create(server.display);

    // Output layout
    server.output_layout = wlr_output_layout_create(server.display);
    wl_list_init(&server.outputs);
    server.new_output.notify = server_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    // Scene graph — create explicit layer trees so z-order is enforced:
    //   bg < bottom < tiling_tree < top < overlay
    server.scene        = wlr_scene_create();
    server.scene_layout = wlr_scene_attach_output_layout(
        server.scene, server.output_layout);

    server.layer_bg     = wlr_scene_tree_create(&server.scene->tree);
    server.layer_bottom = wlr_scene_tree_create(&server.scene->tree);
    server.tiling_tree  = wlr_scene_tree_create(&server.scene->tree);
    server.layer_top    = wlr_scene_tree_create(&server.scene->tree);
    server.layer_overlay= wlr_scene_tree_create(&server.scene->tree);

    // XDG shell (normal application windows)
    wl_list_init(&server.toplevels);
    server.xdg_shell = wlr_xdg_shell_create(server.display, 3);
    server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server.xdg_shell->events.new_toplevel,
        &server.new_xdg_toplevel);
    server.new_xdg_popup.notify = server_new_xdg_popup;
    wl_signal_add(&server.xdg_shell->events.new_popup,
        &server.new_xdg_popup);

    // Layer shell (swaybg, waybar, etc.)
    wl_list_init(&server.layer_surfaces);
    server.layer_shell = wlr_layer_shell_v1_create(server.display, 4);
    server.new_layer_surface.notify = server_new_layer_surface;
    wl_signal_add(&server.layer_shell->events.new_surface,
        &server.new_layer_surface);

    // Cursor
    server.cursor     = wlr_cursor_create();
    server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    server.cursor_mode = VOCWM_CURSOR_PASSTHROUGH;
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
    setup_cursor_listeners(&server);

    // Input / seat
    wl_list_init(&server.keyboards);
    server.new_input.notify = server_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);

    server.seat = wlr_seat_create(server.display, "seat0");
    server.request_cursor.notify = seat_request_cursor;
    wl_signal_add(&server.seat->events.request_set_cursor,
        &server.request_cursor);
    server.request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server.seat->events.request_set_selection,
        &server.request_set_selection);

    // default config thingy
    const char *home = getenv("HOME");
    const char *data_dir = getenv("VOCWM_DATA_DIR");
    if (!data_dir) data_dir = "/usr/local/share/vocwm";
    if (home) {
        char config_dir[512], user_init[512], default_init[512];
        snprintf(config_dir,   sizeof(config_dir),   "%s/.config/vocwm", home);
        snprintf(user_init,    sizeof(user_init),     "%s/init.lua", config_dir);
        snprintf(default_init, sizeof(default_init),  "%s/init.lua", data_dir);

        // mkdir -p ~/.config/vocwm
        mkdir(config_dir, 0755); // needs #include <sys/stat.h>

        // if user has no init.lua yet, seed it from the shipped default
        if (access(user_init, F_OK) != 0) {
            char cmd[1024];
            snprintf(cmd, sizeof(cmd), "cp '%s' '%s'", default_init, user_init);
            system(cmd);
            wlr_log(WLR_INFO, "Created default config at %s", user_init);
        }
    }

    // Tiling config is intentionally zeroed here.
    // init.lua sets all values; we only provide C-level emergency fallbacks
    // inside lua_init() if init.lua fails to load.
    lua_init(&server);

    // Socket
    const char *socket = wl_display_add_socket_auto(server.display);
    if (!socket) {
        wlr_backend_destroy(server.backend);
        return 1;
    }

    if (!wlr_backend_start(server.backend)) {
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.display);
        return 1;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    setenv("XDG_CURRENT_DESKTOP", "vocwm", true);

    std::cout << "vocwm initialized! :3" << std::endl;
    std::cout << "WAYLAND_DISPLAY=" << socket << std::endl;

    wl_display_run(server.display);

    wl_display_destroy_clients(server.display);
    wl_display_destroy(server.display);
    return 0;
}