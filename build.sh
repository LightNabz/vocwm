#!/bin/bash
meson setup build --reconfigure
meson compile -C build

echo "Build complete. You can run vocwm with './build/vocwm'."