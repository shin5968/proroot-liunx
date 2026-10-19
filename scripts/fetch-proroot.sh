#!/usr/bin/env bash
set -euo pipefail
# Unmodified binaries may only be redistributed within a complete application.
version=v1.2.8
dest=app/src/main/jniLibs/arm64-v8a
mkdir -p "$dest"
while read -r hash name; do
  curl --fail --location --retry 3 "https://github.com/coderredlab/proroot/releases/download/$version/$name" -o "$dest/$name"
  printf '%s  %s\n' "$hash" "$dest/$name" | sha256sum --check
 done <<'HASHES'
a4e74d75b66cdc02b080adfe863dbf9951c3b30610d77beddc95488d5fe5de01 libproroot.so
8c47a0a7db32d84c179ebb5bf3640f655a3181860ece5886ae44d92858730c34 libproroot-runtime.so
1c5bc9537a270e8bf8b1c70222813f57b60b828bfb5503ddf8fe37685092de2f libproroot-bridge.so
51a0ec5bfed00e572a0de09e22d9057e2befc386b78e426613d3e0ab03f4ecee libproroot-linker.so
06c6624db3bdc45b9ced151cd781df439a37b47731d244b93e9d6a58cd48cde0 libproroot-stub-loader.so
HASHES
