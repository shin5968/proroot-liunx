#include <vulkan/vulkan.h>
#include <wayland-client.h>
#include <xcb/xcb.h>
#include <xcb/dri3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

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
static void render_probe(VkPhysicalDevice gpu) {
    VkDevice device=VK_NULL_HANDLE; VkBuffer buffer=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE; VkFence fence=VK_NULL_HANDLE;
    VkResult result=VK_SUCCESS; const char *stage="queue"; int matched=0, exported=0;
    uint32_t n=0; vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,NULL);
    VkQueueFamilyProperties *families=calloc(n?n:1,sizeof(*families));
    vkGetPhysicalDeviceQueueFamilyProperties(gpu,&n,families);
    uint32_t family=0;while(family<n&&!(families[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))family++;
    free(families);
    if(family==n){result=VK_ERROR_FEATURE_NOT_PRESENT;goto done;}
    float priority=1;
    VkDeviceQueueCreateInfo queue={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,.queueFamilyIndex=family,.queueCount=1,.pQueuePriorities=&priority};
    const char *extensions[]={"VK_KHR_external_memory_fd","VK_EXT_external_memory_dma_buf"};
    VkDeviceCreateInfo create={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,.queueCreateInfoCount=1,.pQueueCreateInfos=&queue,.enabledExtensionCount=2,.ppEnabledExtensionNames=extensions};
#define TRY(call,label) do{stage=label;result=(call);if(result!=VK_SUCCESS)goto done;}while(0)
    TRY(vkCreateDevice(gpu,&create,NULL,&device),"vkCreateDevice");
    VkExternalMemoryBufferCreateInfo external={.sType=VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkBufferCreateInfo bi={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,.pNext=&external,.size=4096,.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT};
    TRY(vkCreateBuffer(device,&bi,NULL,&buffer),"vkCreateBuffer");
    VkMemoryRequirements requirements;vkGetBufferMemoryRequirements(device,buffer,&requirements);
    VkPhysicalDeviceMemoryProperties properties;vkGetPhysicalDeviceMemoryProperties(gpu,&properties);
    uint32_t type=0;
    for(;type<properties.memoryTypeCount;type++)
        if((requirements.memoryTypeBits&(1u<<type))&&
           (properties.memoryTypes[type].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
           (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))break;
    if(type==properties.memoryTypeCount){result=VK_ERROR_FEATURE_NOT_PRESENT;stage="coherent memory";goto done;}
    VkExportMemoryAllocateInfo export={.sType=VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
    VkMemoryAllocateInfo allocate={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,.pNext=&export,.allocationSize=requirements.size,.memoryTypeIndex=type};
    TRY(vkAllocateMemory(device,&allocate,NULL,&memory),"vkAllocateMemory");
    TRY(vkBindBufferMemory(device,buffer,memory,0),"vkBindBufferMemory");
    VkCommandPoolCreateInfo pi={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,.queueFamilyIndex=family};
    TRY(vkCreateCommandPool(device,&pi,NULL,&pool),"vkCreateCommandPool");
    VkCommandBufferAllocateInfo ci={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,.commandPool=pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VkCommandBuffer command;TRY(vkAllocateCommandBuffers(device,&ci,&command),"vkAllocateCommandBuffers");
    VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    TRY(vkBeginCommandBuffer(command,&begin),"vkBeginCommandBuffer");
    vkCmdFillBuffer(command,buffer,0,4096,0x1a2b3c4d);
    VkMemoryBarrier barrier={.sType=VK_STRUCTURE_TYPE_MEMORY_BARRIER,.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT,.dstAccessMask=VK_ACCESS_HOST_READ_BIT};
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
    TRY(vkEndCommandBuffer(command),"vkEndCommandBuffer");
    VkFenceCreateInfo fi={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    TRY(vkCreateFence(device,&fi,NULL,&fence),"vkCreateFence");
    VkQueue q;vkGetDeviceQueue(device,family,0,&q);
    VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,.commandBufferCount=1,.pCommandBuffers=&command};
    TRY(vkQueueSubmit(q,1,&submit,fence),"vkQueueSubmit");
    TRY(vkWaitForFences(device,1,&fence,VK_TRUE,5000000000ull),"vkWaitForFences");
    uint32_t *words=NULL;TRY(vkMapMemory(device,memory,0,4096,0,(void**)&words),"readback");
    matched=1;for(int i=0;i<1024;i++)if(words[i]!=0x1a2b3c4d)matched=0;
    vkUnmapMemory(device,memory);
    PFN_vkGetMemoryFdKHR getfd=(PFN_vkGetMemoryFdKHR)vkGetDeviceProcAddr(device,"vkGetMemoryFdKHR");
    if(getfd){
        VkMemoryGetFdInfoKHR get={.sType=VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,.memory=memory,.handleType=VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT};
        int fd=-1;VkResult r=getfd(device,&get,&fd);exported=r==VK_SUCCESS&&fd>=0;if(fd>=0)close(fd);
    }
done:
    printf(",\"gpu_test\":{\"stage\":");json_string(stage);
    printf(",\"result\":%d,\"fill_verified\":%s,\"dma_buf_export\":%s,\"cpu_readback_is_diagnostic_only\":true}",result,matched?"true":"false",exported?"true":"false");
    if(device){
        vkDeviceWaitIdle(device);
        if(fence)vkDestroyFence(device,fence,NULL);
        if(pool)vkDestroyCommandPool(device,pool,NULL);
        if(buffer)vkDestroyBuffer(device,buffer,NULL);
        if(memory)vkFreeMemory(device,memory,NULL);
        vkDestroyDevice(device,NULL);
    }
#undef TRY
}
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
    printf("]");
    if(count)render_probe(devices[0]);
    free(devices);
    if (instance) vkDestroyInstance(instance, NULL);
    printf(",\"wayland\":{");
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
