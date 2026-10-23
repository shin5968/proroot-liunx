#!/bin/bash
set -eu
export XDG_RUNTIME_DIR=/tmp/proroot-wayland
export WAYLAND_DISPLAY=wayland-proroot
mkdir -p "$XDG_RUNTIME_DIR"
chmod 700 "$XDG_RUNTIME_DIR"
server= demo=
cleanup() {
    trap - EXIT HUP INT TERM
    [ -z "$demo" ] || kill "$demo" 2>/dev/null || true
    [ -z "$server" ] || kill "$server" 2>/dev/null || true
    wait 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM
/usr/local/libexec/proroot-wayland /tmp/proroot-present.sock "$WAYLAND_DISPLAY" "$1" "$2" &
server=$!
/usr/local/libexec/proroot-wayland-demo &
demo=$!
wait "$server"
