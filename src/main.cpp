#include <iostream>
#include <cstdlib>
#include <unistd.h>
#include "../include/vocwm.h"

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
    #include <wlr/types/wlr_xdg_shell.h>
    #include <wlr/util/log.h>
}

// Declared in cursor.cpp
extern void setup_cursor_listeners(struct VocwmServer *server);



int main(int argc, char *argv[]) {
    wlr_log_init(WLR_DEBUG, NULL);

    struct VocwmServer server = {};
    server.display = wl_display_create();

    server.backend = wlr_backend_autocreate(wl_display_get_event_loop(server.display), NULL);
    if (!server.backend) { wlr_log(WLR_ERROR, "Failed to create backend"); return 1; }

    server.renderer = wlr_renderer_autocreate(server.backend);
    if (!server.renderer) { wlr_log(WLR_ERROR, "Failed to create renderer"); return 1; }
    wlr_renderer_init_wl_display(server.renderer, server.display);

    server.allocator = wlr_allocator_autocreate(server.backend, server.renderer);
    if (!server.allocator) { wlr_log(WLR_ERROR, "Failed to create allocator"); return 1; }

    wlr_compositor_create(server.display, 5, server.renderer);
    wlr_subcompositor_create(server.display);
    wlr_data_device_manager_create(server.display);

    server.output_layout = wlr_output_layout_create(server.display);
    wl_list_init(&server.outputs);
    server.new_output.notify = server_new_output;
    wl_signal_add(&server.backend->events.new_output, &server.new_output);

    server.scene = wlr_scene_create();
    server.scene_layout = wlr_scene_attach_output_layout(server.scene, server.output_layout);

    wl_list_init(&server.toplevels);
    server.xdg_shell = wlr_xdg_shell_create(server.display, 3);
    server.new_xdg_toplevel.notify = server_new_xdg_toplevel;
    wl_signal_add(&server.xdg_shell->events.new_toplevel, &server.new_xdg_toplevel);
    server.new_xdg_popup.notify = server_new_xdg_popup;
    wl_signal_add(&server.xdg_shell->events.new_popup, &server.new_xdg_popup);

    server.cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(server.cursor, server.output_layout);
    server.cursor_mgr = wlr_xcursor_manager_create(NULL, 24);
    setup_cursor_listeners(&server);

    wl_list_init(&server.keyboards);
    server.new_input.notify = server_new_input;
    wl_signal_add(&server.backend->events.new_input, &server.new_input);
    server.seat = wlr_seat_create(server.display, "seat0");
    server.request_cursor.notify = seat_request_cursor;
    wl_signal_add(&server.seat->events.request_set_cursor, &server.request_cursor);
    server.request_set_selection.notify = seat_request_set_selection;
    wl_signal_add(&server.seat->events.request_set_selection, &server.request_set_selection);

    const char *socket = wl_display_add_socket_auto(server.display);
    if (!socket) { wlr_backend_destroy(server.backend); return 1; }

    if (!wlr_backend_start(server.backend)) {
        wlr_backend_destroy(server.backend);
        wl_display_destroy(server.display);
        return 1;
    }

    setenv("WAYLAND_DISPLAY", socket, true);
    std::cout << "vocwm initialized! :3" << std::endl;
    std::cout << "Running on WAYLAND_DISPLAY=" << socket << std::endl;

    wl_display_run(server.display);

    wl_display_destroy_clients(server.display);
    wl_display_destroy(server.display);
    return 0;
}