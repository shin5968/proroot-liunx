#define _GNU_SOURCE
#include <wayland-server.h>
#include "xdg-shell-server-protocol.h"
#include "wayland_transport.h"
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

static struct wl_display *display;
static int bridge = -1, width = 640, height = 960;
static uint32_t sequence;
struct callback { struct wl_list link; struct wl_resource *resource; };
struct surface {
    struct wl_resource *resource, *pending, *xdg, *top;
    struct wl_listener buffer_destroy;
    struct wl_list callbacks;
    bool attached, configured, acknowledged;
    uint32_t configure_serial;
};
static void destroy_request(struct wl_client *c, struct wl_resource *r) { (void)c; wl_resource_destroy(r); }
static void buffer_gone(struct wl_listener *listener, void *data) {
    (void)data;
    struct surface *s = wl_container_of(listener, s, buffer_destroy);
    wl_list_remove(&s->buffer_destroy.link); wl_list_init(&s->buffer_destroy.link); s->pending = NULL;
}
static void callback_gone(struct wl_resource *r) {
    struct callback *cb = wl_resource_get_user_data(r);
    wl_list_remove(&cb->link); free(cb);
}
static void surface_gone(struct wl_resource *r) {
    struct surface *s = wl_resource_get_user_data(r);
    struct callback *cb, *tmp;
    wl_list_for_each_safe(cb, tmp, &s->callbacks, link) wl_resource_destroy(cb->resource);
    wl_list_remove(&s->buffer_destroy.link);
    // Role resources may outlive wl_surface during client teardown.
    if (s->xdg) wl_resource_set_user_data(s->xdg, NULL);
    if (s->top) wl_resource_set_user_data(s->top, NULL);
    free(s);
}
static void attach(struct wl_client *c, struct wl_resource *r, struct wl_resource *buffer, int32_t x, int32_t y) {
    (void)c;
    if (x || y) { wl_resource_post_error(r, WL_SURFACE_ERROR_INVALID_OFFSET, "offset unsupported"); return; }
    struct surface *s = wl_resource_get_user_data(r);
    wl_list_remove(&s->buffer_destroy.link); wl_list_init(&s->buffer_destroy.link);
    s->pending = buffer; s->attached = true;
    if (buffer) wl_resource_add_destroy_listener(buffer, &s->buffer_destroy);
}
static void damage(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h) {
    (void)c; (void)r; (void)x; (void)y; (void)w; (void)h;
}
static void frame(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    struct surface *s = wl_resource_get_user_data(r);
    struct callback *cb = calloc(1, sizeof(*cb));
    if (!cb) { wl_client_post_no_memory(c); return; }
    cb->resource = wl_resource_create(c, &wl_callback_interface, 1, id);
    if (!cb->resource) { free(cb); wl_client_post_no_memory(c); return; }
    wl_resource_set_implementation(cb->resource, NULL, cb, callback_gone);
    wl_list_insert(s->callbacks.prev, &cb->link);
}
static void region_set(struct wl_client *c, struct wl_resource *r, struct wl_resource *region) { (void)c; (void)r; (void)region; }
static int present(struct wl_shm_buffer *buffer) {
    int w = wl_shm_buffer_get_width(buffer), h = wl_shm_buffer_get_height(buffer);
    int stride = wl_shm_buffer_get_stride(buffer);
    uint32_t format = wl_shm_buffer_get_format(buffer);
    if (w <= 0 || h <= 0 || w > (int)PRWL_MAX_DIM || h > (int)PRWL_MAX_DIM || stride < w * 4 ||
        (uint64_t)stride * h > PRWL_MAX_BYTES || (format != WL_SHM_FORMAT_XRGB8888 && format != WL_SHM_FORMAT_ARGB8888)) return -1;
    const size_t bytes = (size_t)stride * h;
    int fd = memfd_create("proroot-wayland-frame", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0 || ftruncate(fd, (off_t)bytes)) { if (fd >= 0) close(fd); return -1; }
    void *pixels = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED) { close(fd); return -1; }
    wl_shm_buffer_begin_access(buffer);
    memcpy(pixels, wl_shm_buffer_get_data(buffer), bytes);
    wl_shm_buffer_end_access(buffer);
    munmap(pixels, bytes);
    if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL)) { close(fd); return -1; }
    struct prwl_frame header = {PRWL_MAGIC, 1, (uint32_t)w, (uint32_t)h, (uint32_t)stride, format, (uint32_t)bytes, ++sequence};
    struct iovec iov = {&header, sizeof(header)};
    union { struct cmsghdr align; char bytes[CMSG_SPACE(sizeof(int))]; } control = {0};
    struct msghdr msg = {.msg_iov = &iov, .msg_iovlen = 1, .msg_control = control.bytes, .msg_controllen = sizeof(control.bytes)};
    struct cmsghdr *cms = CMSG_FIRSTHDR(&msg);
    cms->cmsg_level = SOL_SOCKET; cms->cmsg_type = SCM_RIGHTS; cms->cmsg_len = CMSG_LEN(sizeof(fd));
    memcpy(CMSG_DATA(cms), &fd, sizeof(fd));
    ssize_t n;
    do { n = sendmsg(bridge, &msg, MSG_NOSIGNAL); } while (n < 0 && errno == EINTR);
    close(fd);
    if (n != sizeof(header)) return -1;
    uint32_t ack = 0;
    do { n = recv(bridge, &ack, sizeof(ack), MSG_WAITALL); } while (n < 0 && errno == EINTR);
    return n == sizeof(ack) && ack == header.sequence ? 0 : -1;
}
static void configure(struct surface *s) {
    if (!s->top || s->configured) return;
    struct wl_array states; wl_array_init(&states);
    uint32_t *state = wl_array_add(&states, sizeof(uint32_t));
    if (!state) { wl_resource_post_no_memory(s->top); return; }
    *state = XDG_TOPLEVEL_STATE_ACTIVATED;
    xdg_toplevel_send_configure(s->top, width, height, &states); wl_array_release(&states);
    s->configure_serial = wl_display_next_serial(display);
    xdg_surface_send_configure(s->xdg, s->configure_serial); s->configured = true;
}
static void commit(struct wl_client *c, struct wl_resource *r) {
    struct surface *s = wl_resource_get_user_data(r);
    configure(s);
    if (s->attached && s->pending) {
        if (!s->top || !s->acknowledged) { wl_resource_post_error(r, WL_SURFACE_ERROR_INVALID_SIZE, "configure must be acknowledged first"); return; }
        struct wl_shm_buffer *buffer = wl_shm_buffer_get(s->pending);
        if (!buffer) { wl_resource_post_error(r, WL_SURFACE_ERROR_INVALID_SIZE, "bootstrap supports wl_shm only"); return; }
        if (present(buffer)) { fprintf(stderr, "present failed: %s\n", strerror(errno)); wl_display_terminate(display); return; }
        wl_buffer_send_release(s->pending);
        wl_list_remove(&s->buffer_destroy.link); wl_list_init(&s->buffer_destroy.link); s->pending = NULL;
        if (sequence == 1 || sequence % 120 == 0) fprintf(stderr, "PRESENTED %u wl_shm CPU_COPY\n", sequence);
    }
    s->attached = false;
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    uint32_t ms = (uint32_t)(now.tv_sec * 1000ull + now.tv_nsec / 1000000);
    struct callback *cb, *tmp;
    wl_list_for_each_safe(cb, tmp, &s->callbacks, link) { wl_callback_send_done(cb->resource, ms); wl_resource_destroy(cb->resource); }
    (void)c;
}
static void transform(struct wl_client *c, struct wl_resource *r, int32_t value) {
    (void)c; if (value != WL_OUTPUT_TRANSFORM_NORMAL) wl_resource_post_error(r, WL_SURFACE_ERROR_INVALID_TRANSFORM, "transform unsupported");
}
static void scale(struct wl_client *c, struct wl_resource *r, int32_t value) {
    (void)c; if (value != 1) wl_resource_post_error(r, WL_SURFACE_ERROR_INVALID_SCALE, "scale must be 1");
}
static const struct wl_surface_interface surface_impl = {
    .destroy = destroy_request, .attach = attach, .damage = damage, .frame = frame,
    .set_opaque_region = region_set, .set_input_region = region_set, .commit = commit,
    .set_buffer_transform = transform, .set_buffer_scale = scale, .damage_buffer = damage
};
static void create_surface(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    struct surface *s = calloc(1, sizeof(*s));
    if (!s) { wl_client_post_no_memory(c); return; }
    s->resource = wl_resource_create(c, &wl_surface_interface, wl_resource_get_version(r), id);
    if (!s->resource) { free(s); wl_client_post_no_memory(c); return; }
    wl_list_init(&s->callbacks); wl_list_init(&s->buffer_destroy.link); s->buffer_destroy.notify = buffer_gone;
    wl_resource_set_implementation(s->resource, &surface_impl, s, surface_gone);
}
static const struct wl_region_interface region_impl = {.destroy = destroy_request, .add = damage, .subtract = damage};
static void create_region(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    (void)r; struct wl_resource *region = wl_resource_create(c, &wl_region_interface, 1, id);
    if (region) wl_resource_set_implementation(region, &region_impl, NULL, NULL);
}
static const struct wl_compositor_interface compositor_impl = {.create_surface = create_surface, .create_region = create_region};
static void bind_compositor(struct wl_client *c, void *data, uint32_t version, uint32_t id) {
    (void)data; struct wl_resource *r = wl_resource_create(c, &wl_compositor_interface, version < 4 ? version : 4, id);
    if (r) wl_resource_set_implementation(r, &compositor_impl, NULL, NULL);
}
static void role_gone(struct wl_resource *r) {
    struct surface *s = wl_resource_get_user_data(r);
    if (s) { if (s->xdg == r) s->xdg = NULL; if (s->top == r) s->top = NULL; }
}
static void top_parent(struct wl_client *c, struct wl_resource *r, struct wl_resource *p) { (void)c; (void)r; (void)p; }
static void top_text(struct wl_client *c, struct wl_resource *r, const char *text) { (void)c; (void)r; (void)text; }
static void top_menu(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial, int32_t x, int32_t y) { (void)c; (void)r; (void)seat; (void)serial; (void)x; (void)y; }
static void top_move(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial) { (void)c; (void)r; (void)seat; (void)serial; }
static void top_resize(struct wl_client *c, struct wl_resource *r, struct wl_resource *seat, uint32_t serial, uint32_t edges) { (void)edges; top_move(c,r,seat,serial); }
static void top_size(struct wl_client *c, struct wl_resource *r, int32_t w, int32_t h) { (void)c; (void)r; (void)w; (void)h; }
static void top_noop(struct wl_client *c, struct wl_resource *r) { (void)c; (void)r; }
static const struct xdg_toplevel_interface top_impl = {
    .destroy = destroy_request, .set_parent = top_parent, .set_title = top_text, .set_app_id = top_text,
    .show_window_menu = top_menu, .move = top_move, .resize = top_resize, .set_max_size = top_size, .set_min_size = top_size,
    .set_maximized = top_noop, .unset_maximized = top_noop, .set_fullscreen = top_parent,
    .unset_fullscreen = top_noop, .set_minimized = top_noop
};
static void get_top(struct wl_client *c, struct wl_resource *r, uint32_t id) {
    struct surface *s = wl_resource_get_user_data(r);
    if (!s || s->top) { wl_resource_post_error(r, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED, "one toplevel per surface"); return; }
    s->top = wl_resource_create(c, &xdg_toplevel_interface, 1, id);
    if (s->top) wl_resource_set_implementation(s->top, &top_impl, s, role_gone);
}
static void get_popup(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *parent, struct wl_resource *pos) {
    (void)c; (void)id; (void)parent; (void)pos; wl_resource_post_error(r, XDG_WM_BASE_ERROR_INVALID_POSITIONER, "popups not supported by bootstrap compositor");
}
static void geometry(struct wl_client *c, struct wl_resource *r, int32_t x, int32_t y, int32_t w, int32_t h) { damage(c,r,x,y,w,h); }
static void ack_configure(struct wl_client *c, struct wl_resource *r, uint32_t serial) {
    (void)c; struct surface *s = wl_resource_get_user_data(r);
    if (s && s->configured && serial == s->configure_serial) s->acknowledged = true;
    else wl_resource_post_error(r, XDG_SURFACE_ERROR_INVALID_SERIAL, "unknown configure serial");
}
static const struct xdg_surface_interface xdg_impl = {.destroy = destroy_request, .get_toplevel = get_top, .get_popup = get_popup, .set_window_geometry = geometry, .ack_configure = ack_configure};
static void get_xdg(struct wl_client *c, struct wl_resource *r, uint32_t id, struct wl_resource *surface) {
    (void)r; struct surface *s = wl_resource_get_user_data(surface);
    if (s->xdg) { wl_resource_post_error(r, XDG_WM_BASE_ERROR_ROLE, "surface already has a role"); return; }
    s->xdg = wl_resource_create(c, &xdg_surface_interface, 1, id);
    if (s->xdg) wl_resource_set_implementation(s->xdg, &xdg_impl, s, role_gone);
}
static void positioner(struct wl_client *c, struct wl_resource *r, uint32_t id) { (void)c; (void)id; wl_resource_post_error(r, XDG_WM_BASE_ERROR_INVALID_POSITIONER, "popups unsupported"); }
static void pong(struct wl_client *c, struct wl_resource *r, uint32_t serial) { (void)c; (void)r; (void)serial; }
static const struct xdg_wm_base_interface wm_impl = {.destroy = destroy_request, .create_positioner = positioner, .get_xdg_surface = get_xdg, .pong = pong};
static void bind_wm(struct wl_client *c, void *data, uint32_t version, uint32_t id) {
    (void)data; (void)version; struct wl_resource *r = wl_resource_create(c, &xdg_wm_base_interface, 1, id);
    if (r) wl_resource_set_implementation(r, &wm_impl, NULL, NULL);
}
static void bind_output(struct wl_client *c, void *data, uint32_t version, uint32_t id) {
    (void)data; struct wl_resource *r = wl_resource_create(c, &wl_output_interface, version < 2 ? version : 2, id);
    if (!r) return;
    wl_resource_set_implementation(r, NULL, NULL, NULL);
    wl_output_send_geometry(r, 0, 0, 160, 250, WL_OUTPUT_SUBPIXEL_UNKNOWN, "Proroot", "Android Surface", WL_OUTPUT_TRANSFORM_NORMAL);
    wl_output_send_mode(r, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, width, height, 30000);
    if (wl_resource_get_version(r) >= 2) { wl_output_send_scale(r, 1); wl_output_send_done(r); }
}
static int terminate(int signal, void *data) { (void)signal; (void)data; wl_display_terminate(display); return 0; }
int main(int argc, char **argv) {
    if (argc != 5) { fprintf(stderr, "usage: compositor BRIDGE SOCKET WIDTH HEIGHT\n"); return 2; }
    width = atoi(argv[3]); height = atoi(argv[4]);
    if (width < 1 || height < 1 || width > (int)PRWL_MAX_DIM || height > (int)PRWL_MAX_DIM) return 2;
    signal(SIGPIPE, SIG_IGN);
    bridge = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_un address = {.sun_family = AF_UNIX};
    if (bridge < 0 || strlen(argv[1]) >= sizeof(address.sun_path)) return 3;
    strcpy(address.sun_path, argv[1]);
    if (connect(bridge, (struct sockaddr *)&address, sizeof(address))) { perror("connect Android presenter"); return 3; }
    struct timeval timeout = {.tv_sec = 3};
    setsockopt(bridge, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    display = wl_display_create();
    if (!display || wl_display_init_shm(display)) return 4;
    wl_global_create(display, &wl_compositor_interface, 4, NULL, bind_compositor);
    wl_global_create(display, &xdg_wm_base_interface, 1, NULL, bind_wm);
    wl_global_create(display, &wl_output_interface, 2, NULL, bind_output);
    struct wl_event_loop *loop = wl_display_get_event_loop(display);
    struct wl_event_source *term = wl_event_loop_add_signal(loop, SIGTERM, terminate, NULL);
    struct wl_event_source *hup = wl_event_loop_add_signal(loop, SIGHUP, terminate, NULL);
    if (wl_display_add_socket(display, argv[2])) { perror("Wayland socket"); return 5; }
    fprintf(stderr, "READY WAYLAND_DISPLAY=%s %dx%d wl_shm CPU_COPY\n", argv[2], width, height);
    wl_display_run(display);
    wl_display_destroy_clients(display); wl_event_source_remove(term); wl_event_source_remove(hup);
    wl_display_destroy(display); close(bridge);
    return 0;
}
