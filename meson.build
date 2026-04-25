project('vocwm', ['cpp', 'c'], 
  version: '0.1', 
  default_options: ['cpp_std=c++20', 'c_std=c11']
)

add_project_arguments('-DWLR_USE_UNSTABLE', language: ['c', 'cpp'])

wayland = import('wayland')

wayland_server = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
wlroots        = dependency('wlroots-0.19') 
xkbcommon      = dependency('xkbcommon')

# Find the XML
wl_protocol_dir = wayland_protos.get_variable(pkgconfig: 'pkgdatadir')
xdg_shell_xml = wl_protocol_dir + '/stable/xdg-shell/xdg-shell.xml'

# scan_xml returns a list: [source, client_header, server_header]
# Since we are building a server, we need the server header.
xdg_shell_files = wayland.scan_xml(
    xdg_shell_xml, 
    client: false, 
    server: true, 
    include_core_only: true 
)

executable('vocwm',
  ['src/main.cpp', 'src/keyboard.cpp', 'src/cursor.cpp', 'src/output.cpp', 'src/xdg_surface.cpp', xdg_shell_files],
  include_directories: ['.', 'include'],
  dependencies: [
    wayland_server, 
    wlroots, 
    xkbcommon, 
    wayland_protos
  ],
  install: true
)