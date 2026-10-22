#include <vulkan/vulkan.h>
#include <wayland-client.h>
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int has_extension(VkExtensionProperties *items, uint32_t count, const char *name) {
    for (uint32_t i = 0; i < count; ++i) if (!strcmp(items[i].extensionName, name)) return 1;
    return 0;
}
static void json_string(const char *s) {
    putchar('"');
    for (; *s; ++s) {
        if (*s == '"' || *s == '\\') putchar('\\');
        if ((unsigned char)*s >= 32) putchar(*s);
    }
    putchar('"');
}
static int dmabuf_version, shm_present;
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    (void)data; (void)registry; (void)name;
    if (!strcmp(interface, "zwp_linux_dmabuf_v1")) dmabuf_version = (int)version;
    if (!strcmp(interface, "wl_shm")) shm_present = 1;
}
static void global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    (void)data; (void)registry; (void)name;
}
static const struct wl_registry_listener listener = {global, global_remove};
int main(void) {
    VkApplicationInfo app = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO, .pApplicationName = "Proroot validation", .apiVersion = VK_API_VERSION_1_2};
    VkInstanceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&create, NULL, &instance);
    printf("{\"vkCreateInstance\":%d,\"devices\":[", result);
    uint32_t count = 0;
    if (result == VK_SUCCESS) vkEnumeratePhysicalDevices(instance, &count, NULL);
    VkPhysicalDevice *devices = calloc(count ? count : 1, sizeof(*devices));
    if (count) vkEnumeratePhysicalDevices(instance, &count, devices);
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceDriverProperties driver = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
        VkPhysicalDeviceProperties2 properties = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &driver};
        vkGetPhysicalDeviceProperties2(devices[i], &properties);
        uint32_t n = 0; vkEnumerateDeviceExtensionProperties(devices[i], NULL, &n, NULL);
        VkExtensionProperties *extensions = calloc(n ? n : 1, sizeof(*extensions));
        vkEnumerateDeviceExtensionProperties(devices[i], NULL, &n, extensions);
        if (i) putchar(',');
        printf("{\"name\":"); json_string(properties.properties.deviceName);
        printf(",\"driver\":"); json_string(driver.driverName);
        printf(",\"driverInfo\":"); json_string(driver.driverInfo);
        printf(",\"driverID\":%u,\"vendorID\":%u,\"dma_buf\":%s,\"external_memory_fd\":%s,\"drm_modifiers\":%s,\"ahb\":%s}",
               driver.driverID, properties.properties.vendorID,
               has_extension(extensions,n,"VK_EXT_external_memory_dma_buf") ? "true":"false",
               has_extension(extensions,n,"VK_KHR_external_memory_fd") ? "true":"false",
               has_extension(extensions,n,"VK_EXT_image_drm_format_modifier") ? "true":"false",
               has_extension(extensions,n,"VK_ANDROID_external_memory_android_hardware_buffer") ? "true":"false");
        free(extensions);
    }
    free(devices);
    if (instance) vkDestroyInstance(instance, NULL);
    printf("],\"wayland\":{");
    const char *wayland = getenv("WAYLAND_DISPLAY");
    struct wl_display *display = wayland ? wl_display_connect(NULL) : NULL;
    if (display) {
        struct wl_registry *registry = wl_display_get_registry(display);
        wl_registry_add_listener(registry, &listener, NULL);
        int r = wl_display_roundtrip(display);
        printf("\"connected\":%s,\"linux_dmabuf_version\":%d,\"wl_shm\":%s", r >= 0 ? "true":"false", dmabuf_version, shm_present ? "true":"false");
        wl_registry_destroy(registry); wl_display_disconnect(display);
    } else printf("\"connected\":false,\"linux_dmabuf_version\":0,\"status\":\"NOT_TESTED_NO_COMPOSITOR\"");
    printf("},\"x11\":{");
    const char *xdisplay = getenv("DISPLAY");
    xcb_connection_t *connection = xdisplay ? xcb_connect(NULL, NULL) : NULL;
    if (connection && !xcb_connection_has_error(connection)) {
        xcb_dri3_query_version_cookie_t cookie = xcb_dri3_query_version(connection, 1, 2);
        xcb_dri3_query_version_reply_t *reply = xcb_dri3_query_version_reply(connection, cookie, NULL);
        printf("\"connected\":true,\"dri3\":%s,\"major\":%u,\"minor\":%u", reply ? "true":"false", reply ? reply->major_version:0, reply ? reply->minor_version:0);
        free(reply);
    } else printf("\"connected\":false,\"status\":\"NOT_TESTED_NO_X_SERVER\"");
    if (connection) xcb_disconnect(connection);
    printf("},\"end_to_end_zero_copy\":\"NOT_VERIFIED\"}\n");
    return result == VK_SUCCESS && count > 0 ? 0 : 1;
}
