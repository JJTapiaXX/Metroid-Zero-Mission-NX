/* SPDX-License-Identifier: GPL-3.0-or-later */
/* OunceVK presentation for the existing RGBA software compositor. No shaders,
 * SDK GPU blobs, or game assets are embedded in this backend. */
#include <switch.h>
#define VK_USE_PLATFORM_VI_NN 1
#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "presenter_ounce.h"
#include "screen_overlay.h"
#include "diagnostics.h"

static struct {
    VkInstance instance;
    VkSurfaceKHR surface;
    VkPhysicalDevice physical;
    VkDevice device;
    VkQueue queue;
    uint32_t family, count;
    VkSwapchainKHR swapchain;
    VkExtent2D extent;
    VkImage *images;
    VkSemaphore *rendered;
    VkCommandPool pool;
    VkCommandBuffer cmd;
    VkSemaphore acquired;
    VkFence fence;
    VkBuffer staging;
    VkDeviceMemory staging_memory, texture_memory;
    VkImage texture;
    void *pixels;
    uint32_t *canvas;
    uint64_t wait_ns,compose_start,report_time;
    uint64_t previous_begin,frame_gap,peak_gap;
    bool coherent, texture_initialized, begun;
} r;

static bool check(VkResult result, const char *operation) {
    if (result == VK_SUCCESS) return true;
    char message[160];
    snprintf(message,sizeof(message),"OunceVK %s failed (%d)",operation,(int)result);
    Diagnostics_Event(message);
    return false;
}
#define VK_OK(call) do { if (!check((call), #call)) goto fail; } while (0)

static bool memory_type(uint32_t bits, VkMemoryPropertyFlags required,
                        VkMemoryPropertyFlags preferred, uint32_t *index) {
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(r.physical,&props);
    for (unsigned pass=0;pass<2;++pass)
        for (uint32_t i=0;i<props.memoryTypeCount;++i) {
            VkMemoryPropertyFlags flags=props.memoryTypes[i].propertyFlags;
            if ((bits&(1u<<i)) && (flags&required)==required &&
                (pass || (flags&preferred)==preferred)) {
                *index=i;
                return true;
            }
        }
    Diagnostics_Event("OunceVK no compatible memory type");
    return false;
}

static void destroy_swapchain(void) {
    if (r.rendered) for (uint32_t i=0;i<r.count;++i)
        if (r.rendered[i]) vkDestroySemaphore(r.device,r.rendered[i],NULL);
    free(r.rendered); free(r.images);
    r.rendered=NULL; r.images=NULL; r.count=0;
    if (r.swapchain) vkDestroySwapchainKHR(r.device,r.swapchain,NULL);
    r.swapchain=VK_NULL_HANDLE;
}

static bool create_swapchain(void) {
    VkSurfaceCapabilitiesKHR caps;
    VkSurfaceFormatKHR *formats=NULL, chosen={0};
    uint32_t count=0;
    VK_OK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(r.physical,r.surface,&caps));
    if (!(caps.supportedUsageFlags&VK_IMAGE_USAGE_TRANSFER_DST_BIT)) goto fail;
    VK_OK(vkGetPhysicalDeviceSurfaceFormatsKHR(r.physical,r.surface,&count,NULL));
    if (!count) goto fail;
    formats=calloc(count,sizeof(*formats)); if (!formats) goto fail;
    VK_OK(vkGetPhysicalDeviceSurfaceFormatsKHR(r.physical,r.surface,&count,formats));
    for (uint32_t i=0;i<count;++i) {
        if (formats[i].format==VK_FORMAT_UNDEFINED) formats[i].format=VK_FORMAT_R8G8B8A8_UNORM;
        VkFormatProperties props;
        vkGetPhysicalDeviceFormatProperties(r.physical,formats[i].format,&props);
        if (props.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_DST_BIT) {
            chosen=formats[i];
            if (chosen.format==VK_FORMAT_R8G8B8A8_UNORM) break;
        }
    }
    free(formats); formats=NULL;
    if (!chosen.format) goto fail;
    r.extent=caps.currentExtent;
    if (r.extent.width==UINT32_MAX) {
        r.extent=(VkExtent2D){SWITCH_SCREEN_WIDTH,SWITCH_SCREEN_HEIGHT};
        if (r.extent.width<caps.minImageExtent.width) r.extent.width=caps.minImageExtent.width;
        if (r.extent.width>caps.maxImageExtent.width) r.extent.width=caps.maxImageExtent.width;
        if (r.extent.height<caps.minImageExtent.height) r.extent.height=caps.minImageExtent.height;
        if (r.extent.height>caps.maxImageExtent.height) r.extent.height=caps.maxImageExtent.height;
    }
    if (!r.extent.width || !r.extent.height) goto fail;
    uint32_t image_count=caps.minImageCount+1;
    if (caps.maxImageCount && image_count>caps.maxImageCount) image_count=caps.maxImageCount;
    VkCompositeAlphaFlagBitsKHR alpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    if (!(caps.supportedCompositeAlpha&alpha))
        alpha=(VkCompositeAlphaFlagBitsKHR)(caps.supportedCompositeAlpha&(~caps.supportedCompositeAlpha+1));
    const VkSwapchainCreateInfoKHR info={
        .sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface=r.surface,.minImageCount=image_count,
        .imageFormat=chosen.format,.imageColorSpace=chosen.colorSpace,
        .imageExtent=r.extent,.imageArrayLayers=1,
        .imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .imageSharingMode=VK_SHARING_MODE_EXCLUSIVE,
        .preTransform=caps.currentTransform,.compositeAlpha=alpha,
        .presentMode=VK_PRESENT_MODE_FIFO_KHR,.clipped=VK_TRUE,
    };
    VK_OK(vkCreateSwapchainKHR(r.device,&info,NULL,&r.swapchain));
    VK_OK(vkGetSwapchainImagesKHR(r.device,r.swapchain,&r.count,NULL));
    if (!r.count) goto fail;
    r.images=calloc(r.count,sizeof(*r.images));
    r.rendered=calloc(r.count,sizeof(*r.rendered));
    if (!r.images || !r.rendered) goto fail;
    VK_OK(vkGetSwapchainImagesKHR(r.device,r.swapchain,&r.count,r.images));
    const VkSemaphoreCreateInfo semaphore={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    /* A present-wait semaphore belongs to each image: the submission fence
     * alone does not guarantee that presentation has consumed a semaphore. */
    for (uint32_t i=0;i<r.count;++i)
        VK_OK(vkCreateSemaphore(r.device,&semaphore,NULL,&r.rendered[i]));
    return true;
fail:
    free(formats); destroy_swapchain(); return false;
}

bool PresenterOunce_Init(void) {
    if (r.device) return false;
    nwindowSetDimensions(nwindowGetDefault(),SWITCH_SCREEN_WIDTH,SWITCH_SCREEN_HEIGHT);
    const char *extensions[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_NN_VI_SURFACE_EXTENSION_NAME};
    const VkApplicationInfo app={.sType=VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName="Metroid Zero Mission NX Switch 2",.apiVersion=VK_API_VERSION_1_1};
    const VkInstanceCreateInfo instance={.sType=VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo=&app,.enabledExtensionCount=2,.ppEnabledExtensionNames=extensions};
    VK_OK(vkCreateInstance(&instance,NULL,&r.instance));
    const VkViSurfaceCreateInfoNN surface={.sType=VK_STRUCTURE_TYPE_VI_SURFACE_CREATE_INFO_NN,
        .window=nwindowGetDefault()};
    VK_OK(vkCreateViSurfaceNN(r.instance,&surface,NULL,&r.surface));
    uint32_t count=1;
    VkResult enumerate=vkEnumeratePhysicalDevices(r.instance,&count,&r.physical);
    if ((enumerate!=VK_SUCCESS && enumerate!=VK_INCOMPLETE) || !count) goto fail;
    vkGetPhysicalDeviceQueueFamilyProperties(r.physical,&count,NULL);
    VkQueueFamilyProperties *families=calloc(count,sizeof(*families));
    if (!families) goto fail;
    vkGetPhysicalDeviceQueueFamilyProperties(r.physical,&count,families);
    bool found=false;
    for (uint32_t i=0;i<count;++i) {
        VkBool32 present=VK_FALSE;
        VkResult result=vkGetPhysicalDeviceSurfaceSupportKHR(r.physical,i,r.surface,&present);
        if (result==VK_SUCCESS && present && families[i].queueCount &&
            (families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)) {
            r.family=i; found=true; break;
        }
    }
    free(families); if (!found) goto fail;
    float priority=1.0f;
    const VkDeviceQueueCreateInfo queue={.sType=VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex=r.family,.queueCount=1,.pQueuePriorities=&priority};
    const char *device_extensions[]={VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    const VkDeviceCreateInfo device={.sType=VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount=1,.pQueueCreateInfos=&queue,
        .enabledExtensionCount=1,.ppEnabledExtensionNames=device_extensions};
    VK_OK(vkCreateDevice(r.physical,&device,NULL,&r.device));
    vkGetDeviceQueue(r.device,r.family,0,&r.queue);
    if (!create_swapchain()) goto fail;
    const VkCommandPoolCreateInfo pool={.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,.queueFamilyIndex=r.family};
    VK_OK(vkCreateCommandPool(r.device,&pool,NULL,&r.pool));
    const VkCommandBufferAllocateInfo cmd={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool=r.pool,.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY,.commandBufferCount=1};
    VK_OK(vkAllocateCommandBuffers(r.device,&cmd,&r.cmd));
    const VkSemaphoreCreateInfo semaphore={.sType=VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VK_OK(vkCreateSemaphore(r.device,&semaphore,NULL,&r.acquired));
    const VkFenceCreateInfo fence={.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags=VK_FENCE_CREATE_SIGNALED_BIT};
    VK_OK(vkCreateFence(r.device,&fence,NULL,&r.fence));
    const VkBufferCreateInfo buffer={.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size=SWITCH_SCREEN_WIDTH*SWITCH_SCREEN_HEIGHT*4,
        .usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT,.sharingMode=VK_SHARING_MODE_EXCLUSIVE};
    VK_OK(vkCreateBuffer(r.device,&buffer,NULL,&r.staging));
    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(r.device,r.staging,&requirements);
    uint32_t index;
    if (!memory_type(requirements.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                     VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&index)) goto fail;
    VkPhysicalDeviceMemoryProperties props;
    vkGetPhysicalDeviceMemoryProperties(r.physical,&props);
    r.coherent=(props.memoryTypes[index].propertyFlags&VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)!=0;
    char description[128];
    snprintf(description,sizeof(description),"OunceVK staging memory type=%u flags=0x%x coherent=%u; cached CPU canvas enabled",
        index,(unsigned)props.memoryTypes[index].propertyFlags,(unsigned)r.coherent);
    Diagnostics_Event(description);
    VkMemoryAllocateInfo memory={.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize=requirements.size,.memoryTypeIndex=index};
    VK_OK(vkAllocateMemory(r.device,&memory,NULL,&r.staging_memory));
    VK_OK(vkBindBufferMemory(r.device,r.staging,r.staging_memory,0));
    VK_OK(vkMapMemory(r.device,r.staging_memory,0,VK_WHOLE_SIZE,0,&r.pixels));
    /* HOST_VISIBLE/COHERENT does not mean CPU-cached. Layout scaling copies
     * previous output rows, and the popup dims the entire output in place.
     * Never perform those CPU reads on a potentially uncached GPU mapping. */
    r.canvas=malloc(SWITCH_SCREEN_WIDTH*SWITCH_SCREEN_HEIGHT*sizeof(*r.canvas));
    if (!r.canvas) goto fail;
    VkFormatProperties format;
    vkGetPhysicalDeviceFormatProperties(r.physical,VK_FORMAT_R8G8B8A8_UNORM,&format);
    if (!(format.optimalTilingFeatures&VK_FORMAT_FEATURE_BLIT_SRC_BIT)) goto fail;
    const VkImageCreateInfo image={.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType=VK_IMAGE_TYPE_2D,.format=VK_FORMAT_R8G8B8A8_UNORM,
        .extent={SWITCH_SCREEN_WIDTH,SWITCH_SCREEN_HEIGHT,1},.mipLevels=1,.arrayLayers=1,
        .samples=VK_SAMPLE_COUNT_1_BIT,.tiling=VK_IMAGE_TILING_OPTIMAL,
        .usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode=VK_SHARING_MODE_EXCLUSIVE,.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED};
    VK_OK(vkCreateImage(r.device,&image,NULL,&r.texture));
    vkGetImageMemoryRequirements(r.device,r.texture,&requirements);
    if (!memory_type(requirements.memoryTypeBits,0,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&index)) goto fail;
    memory.allocationSize=requirements.size; memory.memoryTypeIndex=index;
    VK_OK(vkAllocateMemory(r.device,&memory,NULL,&r.texture_memory));
    VK_OK(vkBindImageMemory(r.device,r.texture,r.texture_memory,0));
    Diagnostics_Event("Switch 2 OunceVK: cached CPU canvas, write-only staging upload");
    return true;
fail:
    Diagnostics_Event("Switch 2 OunceVK initialization failed");
    PresenterOunce_Close(); return false;
}

uint32_t *PresenterOunce_Begin(uint32_t *stride_bytes) {
    uint64_t start=armTicksToNs(armGetSystemTick());
    if (!r.device || r.begun ||
        !check(vkWaitForFences(r.device,1,&r.fence,VK_TRUE,UINT64_MAX),"wait frame")) return NULL;
    r.compose_start=armTicksToNs(armGetSystemTick());
    r.wait_ns=r.compose_start-start;
    r.frame_gap=r.previous_begin ? start-r.previous_begin:0;
    if (r.frame_gap>r.peak_gap) r.peak_gap=r.frame_gap;
    r.previous_begin=start;
    *stride_bytes=SWITCH_SCREEN_WIDTH*4; r.begun=true; return r.canvas;
}

static void barrier(VkImage image, VkImageLayout old, VkImageLayout next,
                    VkAccessFlags before, VkAccessFlags after,
                    VkPipelineStageFlags source, VkPipelineStageFlags destination) {
    const VkImageMemoryBarrier b={.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask=before,.dstAccessMask=after,.oldLayout=old,.newLayout=next,
        .srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED,
        .image=image,.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1}};
    vkCmdPipelineBarrier(r.cmd,source,destination,0,0,NULL,0,NULL,1,&b);
}

bool PresenterOunce_End(void) {
    if (!r.begun) return false;
    r.begun=false;
    uint64_t upload_start=armTicksToNs(armGetSystemTick());
    /* A sequential write-only upload is fast even on write-combined memory.
     * The completed canvas remains cached for all compositor read/modify/write
     * operations; no part of the compositor sees the GPU mapping. */
    memcpy(r.pixels,r.canvas,SWITCH_SCREEN_WIDTH*SWITCH_SCREEN_HEIGHT*sizeof(*r.canvas));
    if (!r.coherent) {
        const VkMappedMemoryRange range={.sType=VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .memory=r.staging_memory,.offset=0,.size=VK_WHOLE_SIZE};
        VK_OK(vkFlushMappedMemoryRanges(r.device,1,&range));
    }
    uint64_t submit_start=armTicksToNs(armGetSystemTick());
    uint32_t index;
    VkResult result=vkAcquireNextImageKHR(r.device,r.swapchain,UINT64_MAX,r.acquired,VK_NULL_HANDLE,&index);
    if (result==VK_ERROR_OUT_OF_DATE_KHR) {
        VK_OK(vkDeviceWaitIdle(r.device)); destroy_swapchain();
        if (!create_swapchain()) goto fail;
        result=vkAcquireNextImageKHR(r.device,r.swapchain,UINT64_MAX,r.acquired,VK_NULL_HANDLE,&index);
    }
    if (result!=VK_SUBOPTIMAL_KHR && !check(result,"acquire image")) goto fail;
    VK_OK(vkResetCommandBuffer(r.cmd,0));
    const VkCommandBufferBeginInfo begin={.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    VK_OK(vkBeginCommandBuffer(r.cmd,&begin));
    barrier(r.texture,r.texture_initialized?VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,r.texture_initialized?VK_ACCESS_TRANSFER_READ_BIT:0,
        VK_ACCESS_TRANSFER_WRITE_BIT,r.texture_initialized?VK_PIPELINE_STAGE_TRANSFER_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT);
    const VkBufferImageCopy copy={.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
        .imageExtent={SWITCH_SCREEN_WIDTH,SWITCH_SCREEN_HEIGHT,1}};
    vkCmdCopyBufferToImage(r.cmd,r.staging,r.texture,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
    barrier(r.texture,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    barrier(r.images[index],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,
        VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
    const VkImageBlit blit={
        .srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
        .srcOffsets={{0,0,0},{SWITCH_SCREEN_WIDTH,SWITCH_SCREEN_HEIGHT,1}},
        .dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1},
        .dstOffsets={{0,0,0},{(int32_t)r.extent.width,(int32_t)r.extent.height,1}}};
    vkCmdBlitImage(r.cmd,r.texture,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        r.images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_NEAREST);
    barrier(r.images[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_ACCESS_TRANSFER_WRITE_BIT,0,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
    VK_OK(vkEndCommandBuffer(r.cmd));
    const VkPipelineStageFlags wait=VK_PIPELINE_STAGE_TRANSFER_BIT;
    const VkSubmitInfo submit={.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount=1,.pWaitSemaphores=&r.acquired,.pWaitDstStageMask=&wait,
        .commandBufferCount=1,.pCommandBuffers=&r.cmd,
        .signalSemaphoreCount=1,.pSignalSemaphores=&r.rendered[index]};
    VK_OK(vkResetFences(r.device,1,&r.fence));
    VK_OK(vkQueueSubmit(r.queue,1,&submit,r.fence));
    r.texture_initialized=true;
    const VkPresentInfoKHR present={.sType=VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount=1,.pWaitSemaphores=&r.rendered[index],
        .swapchainCount=1,.pSwapchains=&r.swapchain,.pImageIndices=&index};
    result=vkQueuePresentKHR(r.queue,&present);
    if (result==VK_ERROR_OUT_OF_DATE_KHR || result==VK_SUBOPTIMAL_KHR) {
        VK_OK(vkDeviceWaitIdle(r.device)); destroy_swapchain(); return create_swapchain();
    }
    bool okay=check(result,"present image");
    uint64_t now=armTicksToNs(armGetSystemTick());
    uint64_t compose_ns=upload_start-r.compose_start;
    if (now-r.report_time>=2000000000 ||
        (now-r.report_time>=1000000000 && (now-r.compose_start+r.wait_ns>40000000 || r.frame_gap>40000000))) {
        char message[256];
        snprintf(message,sizeof(message),"gfx frame_gap_us=%llu peak_gap_us=%llu wait_us=%llu compose_us=%llu upload_us=%llu submit_us=%llu layout=%u scale=%u filter=%u",
            (unsigned long long)(r.frame_gap/1000),(unsigned long long)(r.peak_gap/1000),
            (unsigned long long)(r.wait_ns/1000),(unsigned long long)(compose_ns/1000),
            (unsigned long long)((submit_start-upload_start)/1000),(unsigned long long)((now-submit_start)/1000),
            SwitchOverlay_Layout(),SwitchOverlay_Scaling(),SwitchOverlay_Lcd());
        Diagnostics_Event(message);r.report_time=now;r.peak_gap=0;
    }
    return okay;
fail:
    return false;
}

void PresenterOunce_Close(void) {
    if (r.device) {
        vkDeviceWaitIdle(r.device);
        if (r.pixels) vkUnmapMemory(r.device,r.staging_memory);
        if (r.staging) vkDestroyBuffer(r.device,r.staging,NULL);
        if (r.staging_memory) vkFreeMemory(r.device,r.staging_memory,NULL);
        if (r.texture) vkDestroyImage(r.device,r.texture,NULL);
        if (r.texture_memory) vkFreeMemory(r.device,r.texture_memory,NULL);
        if (r.fence) vkDestroyFence(r.device,r.fence,NULL);
        if (r.acquired) vkDestroySemaphore(r.device,r.acquired,NULL);
        if (r.pool) vkDestroyCommandPool(r.device,r.pool,NULL);
        destroy_swapchain(); vkDestroyDevice(r.device,NULL);
    }
    if (r.surface) vkDestroySurfaceKHR(r.instance,r.surface,NULL);
    if (r.instance) vkDestroyInstance(r.instance,NULL);
    free(r.canvas);
    memset(&r,0,sizeof(r));
}

bool PresenterOunce_Warmup(unsigned milliseconds) {
    extern void Achievements_Update(void);
    uint64_t until=armTicksToNs(armGetSystemTick())+(uint64_t)milliseconds*1000000;
    Diagnostics_Event("warming Vulkan presentation before intro");
    do {
        if (!appletMainLoop()) return false;
        /* Drain startup login/cache responses while the intro has not begun,
         * rather than doing that first-use work on its first visible frame. */
        Achievements_Update();
        uint32_t stride;
        uint32_t *pixels=PresenterOunce_Begin(&stride);
        if (!pixels) return false;
        for (unsigned i=0;i<SWITCH_SCREEN_WIDTH*SWITCH_SCREEN_HEIGHT;++i) pixels[i]=0xff000000;
        if (!PresenterOunce_End()) return false;
        uint64_t now=armTicksToNs(armGetSystemTick());
        if (now>=until) break;
        uint64_t remaining=until-now;
        svcSleepThread(remaining<16666667 ? remaining:16666667);
    } while (true);
    Diagnostics_Event("startup warm-up complete; starting game");
    return true;
}
