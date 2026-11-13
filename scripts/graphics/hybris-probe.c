// Offscreen check that guest Vulkan reaches the Android driver through libhybris.
// Prints one JSON object. It needs no window system or compositor.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <vulkan/vulkan.h>

static void text(const char *value) {
    putchar('"');
    for (; *value; ++value) {
        unsigned char c = (unsigned char)*value;
        if (c == '"' || c == '\\') printf("\\%c", c);
        else if (c < 0x20) printf("\\u%04x", c);
        else putchar(c);
    }
    putchar('"');
}

int main(void) {
    Dl_info info;
    memset(&info, 0, sizeof(info));
    printf("{\"vulkan_library\":");
    text(dladdr((void *)vkCreateInstance, &info) && info.dli_fname ? info.dli_fname : "unknown");
    int hybris = info.dli_fname && strstr(info.dli_fname, "/hybris/") != NULL;
    printf(",\"through_libhybris\":%s", hybris ? "true" : "false");

    VkApplicationInfo application = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "proroot-hybris-probe", .apiVersion = VK_API_VERSION_1_1};
    VkInstanceCreateInfo create = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &application};
    VkInstance instance = VK_NULL_HANDLE;
    VkResult result = vkCreateInstance(&create, NULL, &instance);
    printf(",\"create_instance\":%d", (int)result);
    if (result != VK_SUCCESS) { printf(",\"ok\":false}\n"); return 1; }

    VkPhysicalDevice devices[8];
    uint32_t count = 8;
    result = vkEnumeratePhysicalDevices(instance, &count, devices);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE) count = 0;
    printf(",\"enumerate_devices\":%d,\"devices\":[", (int)result);
    int created = 0;
    for (uint32_t i = 0; i < count; ++i) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(devices[i], &p);
        printf("%s{\"name\":", i ? "," : "");
        text(p.deviceName);
        printf(",\"type\":%d,\"vendor\":%u,\"device\":%u,\"api\":\"%u.%u.%u\",\"driver_version\":%u",
            (int)p.deviceType, p.vendorID, p.deviceID, VK_VERSION_MAJOR(p.apiVersion),
            VK_VERSION_MINOR(p.apiVersion), VK_VERSION_PATCH(p.apiVersion), p.driverVersion);
        VkQueueFamilyProperties families[16];
        uint32_t familyCount = 16, family = UINT32_MAX;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &familyCount, families);
        for (uint32_t f = 0; f < familyCount; ++f)
            if (families[f].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family = f; break; }
        if (family == UINT32_MAX) { printf(",\"create_device\":null}"); continue; }
        float priority = 1.0f;
        VkDeviceQueueCreateInfo queue = {.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = family, .queueCount = 1, .pQueuePriorities = &priority};
        VkDeviceCreateInfo deviceInfo = {.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = 1, .pQueueCreateInfos = &queue};
        VkDevice device = VK_NULL_HANDLE;
        result = vkCreateDevice(devices[i], &deviceInfo, NULL, &device);
        printf(",\"create_device\":%d}", (int)result);
        if (result == VK_SUCCESS) { created = 1; vkDeviceWaitIdle(device); vkDestroyDevice(device, NULL); }
    }
    vkDestroyInstance(instance, NULL);
    printf("],\"ok\":%s}\n", created && hybris ? "true" : "false");
    return created && hybris ? 0 : 1;
}
