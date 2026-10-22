#define VK_USE_PLATFORM_ANDROID_KHR
#include <jni.h>
#include <vulkan/vulkan.h>
#include <android/hardware_buffer.h>
#include <android/native_window_jni.h>
#include <android/surface_control.h>
#include <dlfcn.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {
std::mutex lock;
std::string report;
std::string quote(const char* text) {
    std::string s="\"";
    for (;*text;++text) { if (*text=='"' || *text=='\\') s+='\\'; if(static_cast<unsigned char>(*text)>=32)s+=*text; }
    return s+'"';
}
template<class T> T info(VkStructureType type) { T value{}; value.sType=type; return value; }
template<class T> T symbol(const char* name) { return reinterpret_cast<T>(dlsym(RTLD_DEFAULT,name)); }
using CreateControl=ASurfaceControl*(*)(ANativeWindow*,const char*);
using ReleaseControl=void(*)(ASurfaceControl*);
using CreateTransaction=ASurfaceTransaction*(*)();
using TransactionOp=void(*)(ASurfaceTransaction*);
using SetBuffer=void(*)(ASurfaceTransaction*,ASurfaceControl*,AHardwareBuffer*,int);
using SetVisibility=void(*)(ASurfaceTransaction*,ASurfaceControl*,int8_t);
struct State {
    VkInstance instance=VK_NULL_HANDLE; VkDevice device=VK_NULL_HANDLE;
    VkImage image=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE;
    VkCommandPool pool=VK_NULL_HANDLE; VkFence fence=VK_NULL_HANDLE;
    AHardwareBuffer* buffer=nullptr; ASurfaceControl* control=nullptr;
    ~State() {
        if(control) { auto release=symbol<ReleaseControl>("ASurfaceControl_release"); if(release)release(control); }
        if(device) {
            vkDeviceWaitIdle(device);
            if(fence)vkDestroyFence(device,fence,nullptr);
            if(pool)vkDestroyCommandPool(device,pool,nullptr);
            if(image)vkDestroyImage(device,image,nullptr);
            if(memory)vkFreeMemory(device,memory,nullptr);
            vkDestroyDevice(device,nullptr);
        }
        if(buffer)AHardwareBuffer_release(buffer);
        if(instance)vkDestroyInstance(instance,nullptr);
    }
};
std::unique_ptr<State> active;
bool has(const std::vector<VkExtensionProperties>& ext,const char* name) {
    return std::any_of(ext.begin(),ext.end(),[&](const auto& item){return !strcmp(item.extensionName,name);});
}
void check(VkResult result,const char* operation) {
    if(result!=VK_SUCCESS)throw std::string(operation)+": VkResult "+std::to_string(result);
}
std::string probe(JNIEnv* env,jobject surface) {
    auto state=std::make_unique<State>(); std::ostringstream out;
    out<<"{\"scope\":\"ANDROID_HOST_ONLY\",\"requested_linux_driver_commit\":\"d29b3afcecc542119bd548308a34e8dead6f5655\",";
    out<<"\"host_driver_type\":\"ANDROID_SYSTEM_VULKAN\",\"native_drm_nodes\":[";
    struct DrmVersion { int major,minor,patch; size_t nameSize; char* name; size_t dateSize; char* date; size_t descriptionSize; char* description; };
    const char* paths[]={"/dev/dri/renderD128","/dev/dri/renderD129"};
    for(int i=0;i<2;++i) {
        if(i)out<<',';
        const int fd=open(paths[i],O_RDWR|O_CLOEXEC);
        char name[512]{},date[512]{},description[512]{};
        DrmVersion version{};version.nameSize=511;version.name=name;version.dateSize=511;version.date=date;version.descriptionSize=511;version.description=description;
        const int result=fd<0?-1:ioctl(fd,static_cast<int>(_IOWR('d',0,DrmVersion)),&version);
        out<<"{\"path\":"<<quote(paths[i])<<",\"open\":"<<(fd>=0?"true":"false")<<",\"ioctl\":"<<result
           <<",\"name\":"<<quote(name)<<",\"major\":"<<version.major<<",\"minor\":"<<version.minor<<"}";
        if(fd>=0)close(fd);
    }
    out<<"],";
    try {
        auto app=info<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
        app.pApplicationName="Proroot graphics validation"; app.apiVersion=VK_API_VERSION_1_1;
        auto instance=info<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);instance.pApplicationInfo=&app;
        check(vkCreateInstance(&instance,nullptr,&state->instance),"vkCreateInstance");
        uint32_t count=0; check(vkEnumeratePhysicalDevices(state->instance,&count,nullptr),"enumerate count");
        if(!count)throw std::string("No Vulkan physical device");
        std::vector<VkPhysicalDevice> devices(count);check(vkEnumeratePhysicalDevices(state->instance,&count,devices.data()),"enumerate devices");
        auto gpu=devices[0];VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(gpu,&properties);
        out<<"\"host_gpu\":"<<quote(properties.deviceName)<<",\"vendor_id\":"<<properties.vendorID<<",";
        uint32_t n=0;check(vkEnumerateDeviceExtensionProperties(gpu,nullptr,&n,nullptr),"extension count");
        std::vector<VkExtensionProperties> ext(n);check(vkEnumerateDeviceExtensionProperties(gpu,nullptr,&n,ext.data()),"extensions");
        const bool ahb=has(ext,VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME);
        const bool dma=has(ext,"VK_EXT_external_memory_dma_buf"),fd=has(ext,"VK_KHR_external_memory_fd");
        out<<"\"ahb_extension\":"<<(ahb?"true":"false")<<",\"dma_buf_extension\":"<<(dma?"true":"false")
           <<",\"external_memory_fd\":"<<(fd?"true":"false")<<",\"extensions\":[";
        for(size_t i=0;i<ext.size();++i){if(i)out<<',';out<<quote(ext[i].extensionName);}out<<"],";
        auto imageProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(vkGetInstanceProcAddr(state->instance,"vkGetPhysicalDeviceImageFormatProperties2"));
        if(imageProperties) {
            out<<"\"external_image_routes\":[";
            const VkExternalMemoryHandleTypeFlagBits types[]={VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID};
            for(int i=0;i<2;++i) {
                auto handle=info<VkPhysicalDeviceExternalImageFormatInfo>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO);handle.handleType=types[i];
                auto query=info<VkPhysicalDeviceImageFormatInfo2>(VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2);
                query.pNext=&handle;query.format=VK_FORMAT_R8G8B8A8_UNORM;query.type=VK_IMAGE_TYPE_2D;
                query.tiling=VK_IMAGE_TILING_OPTIMAL;query.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
                auto externalProperties=info<VkExternalImageFormatProperties>(VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES);
                auto properties2=info<VkImageFormatProperties2>(VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2);properties2.pNext=&externalProperties;
                const auto result=imageProperties(gpu,&query,&properties2);
                const auto& caps=externalProperties.externalMemoryProperties;
                if(i)out<<',';
                out<<"{\"handle_type\":"<<types[i]<<",\"result\":"<<result<<",\"compatible_handles\":"<<caps.compatibleHandleTypes
                   <<",\"export_from_imported\":"<<caps.exportFromImportedHandleTypes<<",\"features\":"<<caps.externalMemoryFeatures<<"}";
            }
            out<<"],";
        }
        if(!ahb)throw std::string("Host driver lacks Android hardware buffer import");
        uint32_t families=0;vkGetPhysicalDeviceQueueFamilyProperties(gpu,&families,nullptr);
        std::vector<VkQueueFamilyProperties> queues(families);vkGetPhysicalDeviceQueueFamilyProperties(gpu,&families,queues.data());
        uint32_t queue=0;while(queue<families&&!(queues[queue].queueFlags&VK_QUEUE_GRAPHICS_BIT))++queue;
        if(queue==families)throw std::string("No graphics queue");
        float priority=1;
        auto queueInfo=info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
        queueInfo.queueFamilyIndex=queue;queueInfo.queueCount=1;queueInfo.pQueuePriorities=&priority;
        const char* enabled[]={VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,"VK_EXT_queue_family_foreign"};
        auto deviceInfo=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
        deviceInfo.queueCreateInfoCount=1;deviceInfo.pQueueCreateInfos=&queueInfo;
        deviceInfo.enabledExtensionCount=has(ext,"VK_EXT_queue_family_foreign")?2:1;deviceInfo.ppEnabledExtensionNames=enabled;
        check(vkCreateDevice(gpu,&deviceInfo,nullptr,&state->device),"vkCreateDevice");
        ANativeWindow* window=surface?ANativeWindow_fromSurface(env,surface):nullptr;
        if(!window)throw std::string("No Android Surface");
        const uint32_t width=static_cast<uint32_t>(std::max(1,ANativeWindow_getWidth(window)));
        const uint32_t height=static_cast<uint32_t>(std::max(1,ANativeWindow_getHeight(window)));
        auto createControl=symbol<CreateControl>("ASurfaceControl_createFromWindow");
        auto newTransaction=symbol<CreateTransaction>("ASurfaceTransaction_create");
        auto setBuffer=symbol<SetBuffer>("ASurfaceTransaction_setBuffer");
        auto visible=symbol<SetVisibility>("ASurfaceTransaction_setVisibility");
        auto apply=symbol<TransactionOp>("ASurfaceTransaction_apply");
        auto destroy=symbol<TransactionOp>("ASurfaceTransaction_delete");
        if(!createControl||!newTransaction||!setBuffer||!visible||!apply||!destroy){ANativeWindow_release(window);throw std::string("SurfaceControl NDK API unavailable");}
        state->control=createControl(window,"Proroot AHB proof");ANativeWindow_release(window);
        if(!state->control)throw std::string("ASurfaceControl_createFromWindow failed");
        AHardwareBuffer_Desc description{};description.width=width;description.height=height;description.layers=1;
        description.format=AHARDWAREBUFFER_FORMAT_R8G8B8A8_UNORM;
        description.usage=AHARDWAREBUFFER_USAGE_GPU_FRAMEBUFFER|AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE|AHARDWAREBUFFER_USAGE_COMPOSER_OVERLAY;
        int allocated=AHardwareBuffer_allocate(&description,&state->buffer);
        if(allocated)throw std::string("AHardwareBuffer_allocate: ")+std::to_string(allocated);
        int sockets[2];if(socketpair(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0,sockets))throw std::string("socketpair failed");
        AHardwareBuffer* received=nullptr;
        int sent=AHardwareBuffer_sendHandleToUnixSocket(state->buffer,sockets[0]);
        int got=sent?sent:AHardwareBuffer_recvHandleFromUnixSocket(sockets[1],&received);
        close(sockets[0]);close(sockets[1]);
        if(got||!received)throw std::string("AHB handle socket roundtrip failed");
        AHardwareBuffer_release(state->buffer);state->buffer=received;
        auto getProperties=reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(vkGetDeviceProcAddr(state->device,"vkGetAndroidHardwareBufferPropertiesANDROID"));
        if(!getProperties)throw std::string("AHB Vulkan entrypoint unavailable");
        auto format=info<VkAndroidHardwareBufferFormatPropertiesANDROID>(VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID);
        auto bufferProperties=info<VkAndroidHardwareBufferPropertiesANDROID>(VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID);bufferProperties.pNext=&format;
        check(getProperties(state->device,state->buffer,&bufferProperties),"get AHB properties");
        if(format.format!=VK_FORMAT_R8G8B8A8_UNORM)throw std::string("Unsupported AHB format; no conversion fallback");
        auto external=info<VkExternalMemoryImageCreateInfo>(VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO);
        external.handleTypes=VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
        auto imageInfo=info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
        imageInfo.pNext=&external;imageInfo.imageType=VK_IMAGE_TYPE_2D;imageInfo.format=format.format;
        imageInfo.extent={width,height,1};imageInfo.mipLevels=1;imageInfo.arrayLayers=1;imageInfo.samples=VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling=VK_IMAGE_TILING_OPTIMAL;imageInfo.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        check(vkCreateImage(state->device,&imageInfo,nullptr,&state->image),"vkCreateImage");
        auto dedicated=info<VkMemoryDedicatedAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO);dedicated.image=state->image;
        auto import=info<VkImportAndroidHardwareBufferInfoANDROID>(VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID);import.buffer=state->buffer;import.pNext=&dedicated;
        auto memory=info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);memory.pNext=&import;memory.allocationSize=bufferProperties.allocationSize;
        uint32_t memoryType=0;while(memoryType<32&&!(bufferProperties.memoryTypeBits&(1u<<memoryType)))++memoryType;
        if(memoryType==32)throw std::string("No importable AHB memory type");
        memory.memoryTypeIndex=memoryType;
        check(vkAllocateMemory(state->device,&memory,nullptr,&state->memory),"import AHB memory");
        check(vkBindImageMemory(state->device,state->image,state->memory,0),"bind AHB image");
        auto pool=info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);pool.queueFamilyIndex=queue;
        check(vkCreateCommandPool(state->device,&pool,nullptr,&state->pool),"command pool");
        auto commands=info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        commands.commandPool=state->pool;commands.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;commands.commandBufferCount=1;
        VkCommandBuffer command;check(vkAllocateCommandBuffers(state->device,&commands,&command),"command allocation");
        auto begin=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command,&begin),"begin");
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        auto barrier=info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);barrier.image=state->image;barrier.subresourceRange=range;
        barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        barrier.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkClearColorValue color{{0.12f,0.65f,0.40f,1.0f}};
        vkCmdClearColorImage(command,state->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&color,1,&range);
        barrier.oldLayout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=0;
        barrier.srcQueueFamilyIndex=queue;barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_FOREIGN_EXT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        check(vkEndCommandBuffer(command),"end");
        auto fence=info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);check(vkCreateFence(state->device,&fence,nullptr,&state->fence),"fence");
        VkQueue vkQueue;vkGetDeviceQueue(state->device,queue,0,&vkQueue);
        auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        check(vkQueueSubmit(vkQueue,1,&submit,state->fence),"queue submit");
        check(vkWaitForFences(state->device,1,&state->fence,VK_TRUE,5000000000ull),"GPU fence");
        ASurfaceTransaction* transaction=newTransaction();
        setBuffer(transaction,state->control,state->buffer,-1);visible(transaction,state->control,1);apply(transaction);destroy(transaction);
        out<<"\"ahb_socket_roundtrip\":true,\"vk_ahb_import\":true,\"gpu_clear\":true,\"surface_transaction_submitted\":true,"
           <<"\"width\":"<<width<<",\"height\":"<<height<<",\"cpu_pixel_maps\":0,\"pixel_copy_commands\":0,"
           <<"\"synchronization\":\"CPU_WAIT_FENCE_TEST_ONLY\",\"host_path\":\"PASS_SUBMITTED\",";
        active=std::move(state);
    } catch(const std::string& error) {out<<"\"host_path\":\"FAILED\",\"error\":"<<quote(error.c_str())<<",";}
    out<<"\"wayland_dmabuf_to_ahb\":\"NOT_VERIFIED\",\"dri3\":\"NOT_TESTED\",\"end_to_end_zero_copy\":\"NOT_VERIFIED\"}";
    return out.str();
}
}
extern "C" JNIEXPORT jstring JNICALL Java_dev_proroot_app_NativeGraphics_run(JNIEnv* env,jobject,jobject surface) {
    std::lock_guard<std::mutex> guard(lock);active.reset();report=probe(env,surface);return env->NewStringUTF(report.c_str());
}
extern "C" JNIEXPORT void JNICALL Java_dev_proroot_app_NativeGraphics_release(JNIEnv*,jobject) {
    std::lock_guard<std::mutex> guard(lock);active.reset();
}
