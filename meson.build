project('vocwm', ['cpp', 'c'], 
  version: '0.1', 
  default_options: ['cpp_std=c++20', 'c_std=c11']
)

add_project_arguments('-DWLR_USE_UNSTABLE', language: ['c', 'cpp'])

# Dependencies
wayland_server = dependency('wayland-server')
wayland_protos = dependency('wayland-protocols')
wlroots        = dependency('wlroots-0.19') 
xkbcommon      = dependency('xkbcommon')
lua            = dependency('lua5.5', required: false)

if not lua.found()
  lua          = dependency('lua')
endif

# Tooling
wayland_scanner = find_program('wayland-scanner')
wl_protocol_dir = wayland_protos.get_variable(pkgconfig: 'pkgdatadir')

# --- Protocol Generation ---
# This list contains the pairs of [XML Path, Output Base Name]
# We use custom_target to ensure the header is named exactly what wlroots expects.
protocols = [
  [wl_protocol_dir / 'stable/xdg-shell/xdg-shell.xml', 'xdg-shell'],
  [meson.current_source_dir() / 'protocols/wlr-layer-shell-unstable-v1.xml', 'wlr-layer-shell-unstable-v1'],
]

protocol_sources = []

foreach p : protocols
  xml = p[0]
  base = p[1]

  # Generate the .c file (private code)
  protocol_sources += custom_target(
    base + '_c',
    input: xml,
    output: base + '-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )

  # Generate the .h file (header)
  # This makes exactly "wlr-layer-shell-unstable-v1-protocol.h"
  protocol_sources += custom_target(
    base + '_h',
    input: xml,
    output: base + '-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )
endforeach

# --- Executable ---
# include_directories('.') is key so the compiler finds generated files in build/
inc = include_directories('include', '.')

executable('vocwm',
  [
    'src/main.cpp', 
    'src/keyboard.cpp', 
    'src/cursor.cpp', 
    'src/output.cpp', 
    'src/xdg_surface.cpp', 
    'src/tiling.cpp',
    'src/layer_shell.cpp',
    protocol_sources 
  ],
  include_directories: inc,
  dependencies: [
    wayland_server, 
    wlroots, 
    xkbcommon, 
    lua,
  ],
  install: true
)

install_data('share/vocwm/init.lua', install_dir: 'share/vocwm')