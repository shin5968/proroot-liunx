#define _GNU_SOURCE
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

static struct wl_display *display;
static struct wl_compositor *compositor;
static struct wl_shm *shm;
static struct xdg_wm_base *wm;
static struct wl_surface *surface;
static int width = 640, height = 960;
static unsigned frame_number;
static bool running = true, configured;
struct buffer { struct wl_buffer *object; uint32_t *pixels; size_t bytes; bool busy; };
static struct buffer buffers[2];
static void release_buffer(void *data, struct wl_buffer *object) { (void)object; ((struct buffer *)data)->busy = false; }
static const struct wl_buffer_listener buffer_listener = {.release = release_buffer};
static void allocate_buffer(struct buffer *b) {
    b->bytes = (size_t)width * height * 4;
    int fd = memfd_create("proroot-wayland-demo", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)b->bytes)) { perror("demo memfd"); exit(4); }
    b->pixels = mmap(NULL, b->bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (b->pixels == MAP_FAILED) { perror("demo mmap"); exit(4); }
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int)b->bytes);
    b->object = wl_shm_pool_create_buffer(pool, 0, width, height, width * 4, WL_SHM_FORMAT_XRGB8888);
    wl_buffer_add_listener(b->object, &buffer_listener, b); wl_shm_pool_destroy(pool); close(fd);
}
static void paint(void);
static void frame_done(void *data, struct wl_callback *cb, uint32_t time) { (void)data; (void)time; wl_callback_destroy(cb); paint(); }
static const struct wl_callback_listener frame_listener = {.done = frame_done};
static void rect(uint32_t *p, int x, int y, int w, int h, uint32_t color) {
    for (int row = y; row < y + h && row < height; ++row)
        for (int col = x; col < x + w && col < width; ++col)
            if (row >= 0 && col >= 0) p[(size_t)row * width + col] = color;
}
static void paint(void) {
    struct buffer *b = !buffers[0].busy ? &buffers[0] : !buffers[1].busy ? &buffers[1] : NULL;
    if (!b) { fprintf(stderr, "Both buffers busy\n"); running = false; return; }
    if (!b->object) allocate_buffer(b);
    // A Linux Wayland client generates every pixel, including the moving block.
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x)
        b->pixels[(size_t)y * width + x] = ((x / 64 + y / 64) & 1) ? 0xff142b24u : 0xff10221du;
    int margin = width / 12;
    rect(b->pixels, margin, height / 6, width - margin * 2, 12, 0xffa8e6c3);
    rect(b->pixels, margin, height / 6 + 36, width / 2, 12, 0xff659980);
    rect(b->pixels, margin, height / 6 + 60, width / 3, 12, 0xff416e58);
    const int size = width / 6;
    int x = margin + (int)(frame_number * 5 % (unsigned)(width - margin * 2 - size));
    rect(b->pixels, x, height / 2, size, size, 0xffa8e6c3);
    rect(b->pixels, margin, height * 3 / 4, width - margin * 2, 10, 0xff416e58);
    rect(b->pixels, margin, height * 3 / 4, (int)(frame_number % 240) * (width - margin * 2) / 240, 10, 0xffa8e6c3);
    struct wl_callback *cb = wl_surface_frame(surface);
    wl_callback_add_listener(cb, &frame_listener, NULL);
    b->busy = true; wl_surface_attach(surface, b->object, 0, 0);
    wl_surface_damage(surface, 0, 0, width, height); wl_surface_commit(surface);
    ++frame_number;
    if (frame_number == 1 || frame_number % 120 == 0) fprintf(stderr, "DEMO_FRAME %u\n", frame_number);
}
static void xdg_configure(void *data, struct xdg_surface *xdg, uint32_t serial) {
    (void)data; xdg_surface_ack_configure(xdg, serial);
    if (!configured) { configured = true; paint(); }
}
static const struct xdg_surface_listener xdg_listener = {.configure = xdg_configure};
static void top_configure(void *data, struct xdg_toplevel *top, int32_t w, int32_t h, struct wl_array *states) {
    (void)data; (void)top; (void)states;
    if (!configured && w > 0 && h > 0 && w <= 4096 && h <= 4096) { width = w; height = h; }
}
static void top_close(void *data, struct xdg_toplevel *top) { (void)data; (void)top; running = false; }
static const struct xdg_toplevel_listener top_listener = {.configure = top_configure, .close = top_close};
static void ping(void *data, struct xdg_wm_base *base, uint32_t serial) { (void)data; xdg_wm_base_pong(base, serial); }
static const struct xdg_wm_base_listener wm_listener = {.ping = ping};
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    (void)data; (void)version;
    if (!strcmp(interface, "wl_compositor")) compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (!strcmp(interface, "wl_shm")) shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, "xdg_wm_base")) { wm = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1); xdg_wm_base_add_listener(wm, &wm_listener, NULL); }
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) { (void)data; (void)registry; (void)name; }
static const struct wl_registry_listener registry_listener = {.global = global, .global_remove = global_remove};
int main(void) {
    for (int i = 0; i < 100 && !display; ++i) { display = wl_display_connect(NULL); if (!display) usleep(50000); }
    if (!display) { perror("Wayland demo connect"); return 2; }
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);
    if (wl_display_roundtrip(display) < 0 || !compositor || !shm || !wm) return 3;
    surface = wl_compositor_create_surface(compositor);
    struct xdg_surface *xdg = xdg_wm_base_get_xdg_surface(wm, surface);
    xdg_surface_add_listener(xdg, &xdg_listener, NULL);
    struct xdg_toplevel *top = xdg_surface_get_toplevel(xdg);
    xdg_toplevel_add_listener(top, &top_listener, NULL);
    xdg_toplevel_set_title(top, "Proroot Wayland output demo");
    xdg_toplevel_set_app_id(top, "dev.proroot.wayland-demo"); wl_surface_commit(surface);
    while (running && wl_display_dispatch(display) >= 0) {}
    wl_display_disconnect(display); return running ? 1 : 0;
}
