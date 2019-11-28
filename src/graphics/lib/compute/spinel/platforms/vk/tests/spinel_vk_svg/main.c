// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <float.h>
#include <stdio.h>
#include <stdlib.h>

#include "allocator_device.h"
#include "common/macros.h"
#include "common/vk/assert.h"
#include "common/vk/cache.h"
#include "common/vk/debug.h"
#include "common/vk/find_mem_type_idx.h"
#include "common/vk/find_validation_layer.h"
#include "ext/color/color.h"
#include "ext/transform_stack/transform_stack.h"
#include "hotsort/platforms/vk/hotsort_vk.h"
#include "spinel/spinel_assert.h"
#include "spinel/spinel_vk.h"

//
//
//

#include "spinel_vk_find_target.h"

//
//
//

#if defined(SPN_VK_SHADER_INFO_AMD_STATISTICS) || defined(SPN_VK_SHADER_INFO_AMD_DISASSEMBLY)
#include "common/vk/shader_info_amd.h"
#endif

//
// BGRA temporarily disabled because Intel doesn't support STORAGE_IMAGE_BIT.
//
// As soon as the issue is resolved we will remove the redundant format.
//

// clang-format off
#if 0
#define SPN_DEMO_VK_FORMAT          VK_FORMAT_B8G8R8A8_UNORM
#define SPN_DEMO_VK_FORMAT_IS_BGRA  1
#else
#define SPN_DEMO_VK_FORMAT          VK_FORMAT_R8G8B8A8_UNORM
#define SPN_DEMO_VK_FORMAT_IS_BGRA  0
#endif
// clang-format on

//
// Define a platform-specific prefix
//

#ifdef __Fuchsia__
#define VK_PIPELINE_CACHE_PREFIX_STRING "/cache/."
#else
#define VK_PIPELINE_CACHE_PREFIX_STRING "."
#endif

//
//
//

// clang-format off
#define SPN_DEMO_SURFACE_WIDTH  1024
#define SPN_DEMO_SURFACE_HEIGHT 1024
#define SPN_DEMO_SURFACE_PIXELS (SPN_DEMO_SURFACE_WIDTH * SPN_DEMO_SURFACE_HEIGHT)
#define SPN_DEMO_SURFACE_SIZE   (SPN_DEMO_SURFACE_PIXELS * 4 * sizeof(uint8_t))

#define SPN_DEMO_TIMEOUT        (1000UL * 1000UL * 1000UL * 10UL)
// clang-format on

//
//
//

// clang-format off
#define SPN_DEMO_LION_CUB_CHECKSUM_AMD    0x1897BC4F
#define SPN_DEMO_LION_CUB_CHECKSUM_INTEL  0x1894B54C
#define SPN_DEMO_LION_CUB_CHECKSUM_NVIDIA 0x16DA05CE
// clang-format on

//
// FIXME(allanmac): Styling opcodes will be buried later
//

//
// clang-format off
//

#define SPN_STYLING_OPCODE_NOOP                        0

#define SPN_STYLING_OPCODE_COVER_NONZERO               1
#define SPN_STYLING_OPCODE_COVER_EVENODD               2
#define SPN_STYLING_OPCODE_COVER_ACCUMULATE            3
#define SPN_STYLING_OPCODE_COVER_MASK                  4

#define SPN_STYLING_OPCODE_COVER_WIP_ZERO              5
#define SPN_STYLING_OPCODE_COVER_ACC_ZERO              6
#define SPN_STYLING_OPCODE_COVER_MASK_ZERO             7
#define SPN_STYLING_OPCODE_COVER_MASK_ONE              8
#define SPN_STYLING_OPCODE_COVER_MASK_INVERT           9

#define SPN_STYLING_OPCODE_COLOR_FILL_SOLID            10
#define SPN_STYLING_OPCODE_COLOR_FILL_GRADIENT_LINEAR  11

#define SPN_STYLING_OPCODE_COLOR_WIP_ZERO              12
#define SPN_STYLING_OPCODE_COLOR_ACC_ZERO              13

#define SPN_STYLING_OPCODE_BLEND_OVER                  14
#define SPN_STYLING_OPCODE_BLEND_PLUS                  15
#define SPN_STYLING_OPCODE_BLEND_MULTIPLY              16
#define SPN_STYLING_OPCODE_BLEND_KNOCKOUT              17

#define SPN_STYLING_OPCODE_COVER_WIP_MOVE_TO_MASK      18
#define SPN_STYLING_OPCODE_COVER_ACC_MOVE_TO_MASK      19

#define SPN_STYLING_OPCODE_COLOR_ACC_OVER_BACKGROUND   20
#define SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE  21
#define SPN_STYLING_OPCODE_COLOR_ACC_TEST_OPACITY      22

#define SPN_STYLING_OPCODE_COLOR_ILL_ZERO              23
#define SPN_STYLING_OPCODE_COLOR_ILL_COPY_ACC          24
#define SPN_STYLING_OPCODE_COLOR_ACC_MULTIPLY_ILL      25

#define SPN_STYLING_OPCODE_COUNT                       26

//
// clang-format on
//

#ifndef M_PI_F
#define M_PI_F 3.14159265358979323846f
#endif

//
// Temporary forward decls
//

spn_path_t *
lion_cub_paths(spn_path_builder_t pb, uint32_t * const path_count);

spn_raster_t *
lion_cub_rasters(spn_raster_builder_t           rb,
                 struct transform_stack * const ts,
                 uint32_t const                 rotations,
                 spn_path_t const * const       paths,
                 uint32_t const                 path_count,
                 uint32_t * const               raster_count);

spn_layer_id *
lion_cub_composition(spn_composition_t          composition,
                     spn_raster_t const * const rasters,
                     uint32_t const             raster_count,
                     uint32_t * const           layer_count);

void
lion_cub_styling(spn_styling_t              styling,
                 spn_group_id const         group_id,
                 spn_layer_id const * const layer_ids,
                 uint32_t const             layer_count);

//
//
//

struct spn_color
{
#if SPN_DEMO_VK_FORMAT_IS_BGRA
  uint8_t b;
  uint8_t g;
  uint8_t r;
  uint8_t a;
#else
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t a;
#endif
};

static void
spn_buffer_to_ppm(struct spn_color const * const color,
                  uint32_t const                 surface_width,
                  uint32_t const                 surface_height)
{
  FILE * file = fopen("/tmp/surface.ppm", "wb");

  fprintf(file, "P6\n%u %u\n255\n", surface_width, surface_height);

  for (uint32_t ii = 0; ii < surface_width * surface_height; ii++)
    {
      // PPM requires triples in R,G,B order
      uint8_t rgb[3] = { color[ii].r, color[ii].g, color[ii].b };

      fwrite(rgb, 1, 3, file);  // RGB
    }

  fclose(file);
}

//
//
//

static uint32_t
spn_buffer_checksum(uint32_t * buffer, uint32_t const surface_width, uint32_t const surface_height)
{
  uint32_t checksum = 0;

  //
  // FIXME(allanmac): this is fine but maybe use a CRC32 intrinsic or
  // Adler32 -- no need for crypto here!
  //
  for (uint32_t ii = 0; ii < surface_width * surface_height; ii++)
    {
      checksum += buffer[ii] & 0xFFFFFF;  // alpha channel is uninitialized
    }

  return checksum;
}

//
//
//

static void
spn_render_submitter(VkQueue queue, VkFence fence, VkCommandBuffer const cb, void * data)
{
  struct VkSubmitInfo const si = { .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                   .commandBufferCount = 1,
                                   .pCommandBuffers    = &cb };

  vk(QueueSubmit(queue, 1, &si, fence));
}

//
//
//

int
main(int argc, char const * argv[])
{
  //
  // create a Vulkan instances
  //
  VkApplicationInfo const app_info = {

    .sType              = VK_STRUCTURE_TYPE_APPLICATION_INFO,
    .pNext              = NULL,
    .pApplicationName   = "Fuchsia Spinel/VK Test",
    .applicationVersion = 0,
    .pEngineName        = "Fuchsia Spinel/VK",
    .engineVersion      = 0,
    .apiVersion         = VK_API_VERSION_1_1
  };

  char const * const instance_enabled_layers[]     = { vk_find_validation_layer() };
  char const * const instance_enabled_extensions[] = { VK_EXT_DEBUG_REPORT_EXTENSION_NAME };

#ifndef NDEBUG
#define SPN_VK_VALIDATION 1
#endif

  uint32_t const instance_enabled_layer_count =
#ifdef SPN_VK_VALIDATION
    ARRAY_LENGTH_MACRO(instance_enabled_layers)
#else
    0
#endif
    ;

  uint32_t const instance_enabled_extension_count =
#ifdef SPN_VK_VALIDATION
    ARRAY_LENGTH_MACRO(instance_enabled_extensions)
#else
    0
#endif
    ;

  VkInstanceCreateInfo const instance_info = {

    .sType                   = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .pApplicationInfo        = &app_info,
    .enabledLayerCount       = instance_enabled_layer_count,
    .ppEnabledLayerNames     = instance_enabled_layers,
    .enabledExtensionCount   = instance_enabled_extension_count,
    .ppEnabledExtensionNames = instance_enabled_extensions
  };

  VkInstance instance;

  vk(CreateInstance(&instance_info, NULL, &instance));

  //
  //
  //
#ifdef SPN_VK_VALIDATION
  PFN_vkCreateDebugReportCallbackEXT vkCreateDebugReportCallbackEXT =
    (PFN_vkCreateDebugReportCallbackEXT)vkGetInstanceProcAddr(instance,
                                                              "vkCreateDebugReportCallbackEXT");

  PFN_vkDestroyDebugReportCallbackEXT vkDestroyDebugReportCallbackEXT =
    (PFN_vkDestroyDebugReportCallbackEXT)vkGetInstanceProcAddr(instance,
                                                               "vkDestroyDebugReportCallbackEXT");

  struct VkDebugReportCallbackCreateInfoEXT const drcci = {

    .sType = VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT,
    .pNext = NULL,
    .flags = VK_DEBUG_REPORT_INFORMATION_BIT_EXT |          //
             VK_DEBUG_REPORT_WARNING_BIT_EXT |              //
             VK_DEBUG_REPORT_PERFORMANCE_WARNING_BIT_EXT |  //
             VK_DEBUG_REPORT_ERROR_BIT_EXT |                //
             VK_DEBUG_REPORT_DEBUG_BIT_EXT,                 //
    .pfnCallback = vk_debug_report_cb,
    .pUserData   = NULL
  };

  VkDebugReportCallbackEXT drc;

  vk(CreateDebugReportCallbackEXT(instance, &drcci, NULL, &drc));
#endif

  //
  // Prepare Vulkan environment for Spinel
  //
  struct spn_vk_environment environment = { .d   = VK_NULL_HANDLE,
                                            .ac  = NULL,
                                            .pc  = VK_NULL_HANDLE,
                                            .pd  = VK_NULL_HANDLE,
                                            .qfi = 0 };

  //
  // acquire all physical devices
  //
  uint32_t pd_count;

  vk(EnumeratePhysicalDevices(instance, &pd_count, NULL));

  if (pd_count == 0)
    {
      fprintf(stderr, "No device found\n");

      return EXIT_FAILURE;
    }

  VkPhysicalDevice * pds = malloc(pd_count * sizeof(*pds));

  vk(EnumeratePhysicalDevices(instance, &pd_count, pds));

  //
  // select the first device if *both* ids aren't provided
  //
  VkPhysicalDeviceProperties pdp;

  vkGetPhysicalDeviceProperties(pds[0], &pdp);

  uint32_t const vendor_id = (argc <= 2) ? pdp.vendorID : strtoul(argv[1], NULL, 16);
  uint32_t const device_id = (argc <= 2) ? pdp.deviceID : strtoul(argv[2], NULL, 16);

  //
  // list all devices
  //
  environment.pd = VK_NULL_HANDLE;

  for (uint32_t ii = 0; ii < pd_count; ii++)
    {
      VkPhysicalDeviceProperties pdp_tmp;

      vkGetPhysicalDeviceProperties(pds[ii], &pdp_tmp);

      bool const is_match = (pdp_tmp.vendorID == vendor_id) && (pdp_tmp.deviceID == device_id);

      if (is_match)
        {
          pdp            = pdp_tmp;
          environment.pd = pds[ii];
        }

      fprintf(stdout,
              "%c %X : %X : %s\n",
              is_match ? '*' : ' ',
              pdp_tmp.vendorID,
              pdp_tmp.deviceID,
              pdp_tmp.deviceName);
    }

  if (environment.pd == VK_NULL_HANDLE)
    {
      fprintf(stderr, "Device %X : %X not found.\n", vendor_id, device_id);

      return EXIT_FAILURE;
    }

  free(pds);

  //
  // get the physical device's memory props
  //
  vkGetPhysicalDeviceMemoryProperties(environment.pd, &environment.pdmp);

  //
  // get image properties
  //
  // vkGetPhysicalDeviceImageFormatProperties()
  //
  // vk(GetPhysicalDeviceImageFormatProperties(phy_device,
  //

  //
  // get queue properties
  //
  // FIXME(allanmac): The number and composition of queues (compute
  // vs. graphics) will be configured by the target.
  //
  // This implies Spinel/VK needs to either create the queue pool itself
  // or accept an externally defined queue strategy.
  //
  // This is moot until we get Timeline Semaphores and can run on
  // multiple queues.
  //
  uint32_t qfp_count;

  vkGetPhysicalDeviceQueueFamilyProperties(environment.pd, &qfp_count, NULL);

  VkQueueFamilyProperties qfp[qfp_count];

  vkGetPhysicalDeviceQueueFamilyProperties(environment.pd, &qfp_count, qfp);

  //
  // find Spinel target
  //
  struct spn_vk_target const *     spn_target;
  struct hotsort_vk_target const * hs_target;

  char error_message[256];

  if (!spn_vk_find_target(vendor_id,
                          device_id,
                          &spn_target,
                          &hs_target,
                          error_message,
                          sizeof(error_message)))
    {
      fprintf(stderr, "%s\n", error_message);
      return EXIT_FAILURE;
    }

  //
  // probe Spinel device requirements for this target
  //
  struct spn_vk_target_requirements spn_tr = { 0 };

  spn(vk_target_get_requirements(spn_target, &spn_tr));

  //
  // probe HotSort device requirements for this target
  //
  struct hotsort_vk_target_requirements hs_tr = { 0 };

  if (!hotsort_vk_target_get_requirements(hs_target, &hs_tr))
    return EXIT_FAILURE;

  //
  // populate accumulated device requirements
  //
  uint32_t const           ext_name_count = spn_tr.ext_name_count + hs_tr.ext_name_count;
  VkDeviceQueueCreateInfo  qcis[spn_tr.qci_count];
  char const *             ext_names[ext_name_count];
  VkPhysicalDeviceFeatures pdf = { false };

  //
  // populate Spinel device requirements
  //
  spn_tr.qcis      = qcis;
  spn_tr.ext_names = ext_names;
  spn_tr.pdf       = &pdf;

  spn(vk_target_get_requirements(spn_target, &spn_tr));

  //
  // populate HotSort device requirements
  //
  hs_tr.ext_names = ext_names + spn_tr.ext_name_count;
  hs_tr.pdf       = &pdf;

  if (!hotsort_vk_target_get_requirements(hs_target, &hs_tr))
    return EXIT_FAILURE;

  //
  // create VkDevice
  //
#if 0
  //
  // This feature is missing from our prebuilt Vulkan SDK. Enable as
  // soon as the prebuilt SDK is updated.
  //
  VkPhysicalDeviceShaderFloat16Int8FeaturesKHR const shaderFloat16Int8 = {
    .sType         = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR,
    .shaderFloat16 = true,
    .shaderInt8    = true
  };
#endif

  VkDeviceCreateInfo const device_info = {

    .sType                   = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
    .pNext                   = NULL,  // &shaderFloat16Int8,
    .flags                   = 0,
    .queueCreateInfoCount    = spn_tr.qci_count,
    .pQueueCreateInfos       = qcis,
    .enabledLayerCount       = 0,
    .ppEnabledLayerNames     = NULL,
    .enabledExtensionCount   = ext_name_count,
    .ppEnabledExtensionNames = ext_names,
    .pEnabledFeatures        = &pdf
  };

  vk(CreateDevice(environment.pd, &device_info, NULL, &environment.d));

  //
  // create the pipeline cache
  //
  vk_ok(vk_pipeline_cache_create(environment.d,
                                 NULL,
                                 VK_PIPELINE_CACHE_PREFIX_STRING "vk_cache",
                                 &environment.pc));

  //
  // create device perm allocators
  //
  struct spn_allocator_device_perm perm_host_visible;

  {
    VkMemoryPropertyFlags const mpf = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |  //
                                      VK_MEMORY_PROPERTY_HOST_CACHED_BIT |   //
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;  //

    VkBufferUsageFlags const bufb = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  //
                                    VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    spn_allocator_device_perm_create(&perm_host_visible, &environment, mpf, bufb, 0, NULL);
  }

  //
  // allocate surfaces
  //
  struct surface
  {
    struct
    {
      VkImage               image;
      VkDeviceMemory        dm;
      VkDescriptorImageInfo image_info;
    } d;
    struct
    {
      VkDescriptorBufferInfo dbi;
      VkDeviceMemory         dm;
      void *                 map;
    } h;
  } surface;

  //
  // create the image
  //
  VkBufferUsageFlagBits const bufb = VK_IMAGE_USAGE_STORAGE_BIT |       //
                                     VK_IMAGE_USAGE_TRANSFER_SRC_BIT |  //
                                     VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  VkImageCreateInfo const ici = {
    .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext     = NULL,
    .flags     = 0,
    .imageType = VK_IMAGE_TYPE_2D,
    .format    = SPN_DEMO_VK_FORMAT,
    .extent    = { .width = SPN_DEMO_SURFACE_WIDTH, .height = SPN_DEMO_SURFACE_HEIGHT, .depth = 1 },
    .mipLevels = 1,

    .arrayLayers           = 1,
    .samples               = VK_SAMPLE_COUNT_1_BIT,
    .tiling                = VK_IMAGE_TILING_OPTIMAL,
    .usage                 = bufb,
    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices   = NULL,
    .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED
  };

  vk(CreateImage(environment.d, &ici, environment.ac, &surface.d.image));

  //
  // allocate
  //
  VkMemoryRequirements image_mr;

  vkGetImageMemoryRequirements(environment.d, surface.d.image, &image_mr);

  struct VkMemoryAllocateInfo const mai = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = NULL,
    .allocationSize  = image_mr.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&environment.pdmp,
                                            image_mr.memoryTypeBits,
                                            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
  };

  vk(AllocateMemory(environment.d, &mai, environment.ac, &surface.d.dm));

  vk(BindImageMemory(environment.d, surface.d.image, surface.d.dm, 0));

  //
  // create the VkDescriptorImageInfo sampler
  //
  VkSamplerCreateInfo const sci = {

    .sType                   = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
    .pNext                   = NULL,
    .flags                   = 0,
    .magFilter               = VK_FILTER_LINEAR,
    .minFilter               = VK_FILTER_LINEAR,
    .mipmapMode              = VK_SAMPLER_MIPMAP_MODE_NEAREST,
    .addressModeU            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeV            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .addressModeW            = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
    .mipLodBias              = 0.0f,
    .anisotropyEnable        = false,
    .maxAnisotropy           = 0.0f,
    .compareEnable           = false,
    .compareOp               = VK_COMPARE_OP_ALWAYS,
    .minLod                  = 0.0f,
    .maxLod                  = 0.0f,
    .borderColor             = VK_BORDER_COLOR_INT_OPAQUE_WHITE,
    .unnormalizedCoordinates = VK_TRUE
  };

  vk(CreateSampler(environment.d, &sci, environment.ac, &surface.d.image_info.sampler));

  //
  // create the VkDescriptorImageInfo image view
  //
  VkImageViewCreateInfo const ivci = {

    .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .pNext    = NULL,
    .flags    = 0,
    .image    = surface.d.image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format   = ici.format,

    .components = { .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                    .a = VK_COMPONENT_SWIZZLE_IDENTITY },

    .subresourceRange = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                          .baseMipLevel   = 0,
                          .levelCount     = 1,
                          .baseArrayLayer = 0,
                          .layerCount     = 1 }
  };

  vk(CreateImageView(environment.d, &ivci, environment.ac, &surface.d.image_info.imageView));

  //
  // set the VkDescriptorImageInfo image layout
  //
  surface.d.image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  //
  // allocate mapped host buffer
  //
  spn_allocator_device_perm_alloc(&perm_host_visible,  //
                                  &environment,
                                  image_mr.size,
                                  0,
                                  &surface.h.dbi,
                                  &surface.h.dm);

  vk(MapMemory(environment.d, surface.h.dm, 0, VK_WHOLE_SIZE, 0, (void **)&surface.h.map));

  //
  // create a Spinel context
  //
  struct spn_vk_context_create_info const spn_cci = {
    .spinel          = spn_target,
    .hotsort         = hs_target,
    .block_pool_size = 1 << 25,  // 32 MB (128K x 128-dword blocks)
    .handle_count    = 1 << 15,  // 32K handles
  };

  spn_context_t context;

  spn(vk_context_create(&environment, &spn_cci, &context));

  //
  // create a transform stack
  //
  struct transform_stack * const ts = transform_stack_create(16);

  transform_stack_push_scale(ts, 32.0f, 32.0f);

  ////////////////////////////////////
  //
  // SPINEL BOILERPLATE
  //

  //
  // create builders
  //
  spn_path_builder_t pb;

  spn(path_builder_create(context, &pb));

  spn_raster_builder_t rb;

  spn(raster_builder_create(context, &rb));

  //
  // create composition
  //
  spn_composition_t composition;

  spn(composition_create(context, &composition));

  uint32_t const clip[4] = { 0, 0, SPN_DEMO_SURFACE_WIDTH, SPN_DEMO_SURFACE_HEIGHT };

  spn(composition_set_clip(composition, clip));

  //
  // min/max layer in top level group
  //
  uint32_t const layer_count = 4096;

  //
  // create styling
  //
  spn_styling_t styling;

  spn(styling_create(context, &styling, layer_count, 16384));  // 4K layers, 16K cmds

  //
  // set up rendering extensions
  //
  VkBufferImageCopy const bic = {

    .bufferOffset      = 0,
    .bufferRowLength   = SPN_DEMO_SURFACE_WIDTH,
    .bufferImageHeight = SPN_DEMO_SURFACE_HEIGHT,
    .imageSubresource  = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                          .mipLevel       = 0,
                          .baseArrayLayer = 0,
                          .layerCount     = 1 },

    .imageOffset = { .x = 0, .y = 0, .z = 0 },
    .imageExtent = { .width  = SPN_DEMO_SURFACE_WIDTH,
                     .height = SPN_DEMO_SURFACE_HEIGHT,
                     .depth  = 1 }
  };

  spn_vk_render_submit_ext_image_post_copy_to_buffer_t rs_image_post_copy_to_buffer = {

    .ext          = NULL,
    .type         = SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_POST_COPY_TO_BUFFER,
    .dst          = surface.h.dbi.buffer,
    .region_count = 1,
    .regions      = &bic
  };

  VkClearColorValue const ccv = { .float32 = { 1.0f, 1.0f, 1.0f, 1.0f } };

  spn_vk_render_submit_ext_image_pre_clear_t rs_pre_image_clear = {

    .ext   = NULL,
    .type  = SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_PRE_CLEAR,
    .color = &ccv,
  };

  spn_vk_render_submit_ext_image_pre_barrier_t rs_image_pre_barrier = {

    .ext        = NULL,
    .type       = SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_PRE_BARRIER,
    .old_layout = VK_IMAGE_LAYOUT_UNDEFINED,
    .src_qfi    = VK_QUEUE_FAMILY_IGNORED,
  };

  spn_vk_render_submit_ext_image_render_t rs_image_render = {

    .ext            = NULL,
    .type           = SPN_VK_RENDER_SUBMIT_EXT_TYPE_IMAGE_RENDER,
    .image          = surface.d.image,
    .image_info     = surface.d.image_info,
    .submitter_pfn  = spn_render_submitter,
    .submitter_data = NULL
  };

  spn_render_submit_t const rs = {

    .ext         = &rs_image_render,
    .styling     = styling,
    .composition = composition,
    .clip        = { 0, 0, SPN_DEMO_SURFACE_WIDTH, SPN_DEMO_SURFACE_HEIGHT }
  };

  //
  // loop over the entire pipeline
  //
  uint32_t const loop_count = 100 /*60 * 60 * 1*/ /*262144*/;

  for (uint32_t ii = 0; ii < loop_count; ii++)
    {
      fprintf(stderr, "%9u\r:", ii);
      // spn_context_status(context);

      //
      // continuously rotate around the center of the screen
      //
#if 0
      uint32_t const steps = 60;
      float const    theta = M_PI_F * 2.0f * ((float)(ii % steps) / (float)steps);
#else
      float const theta = M_PI_F * 2.0f * ((float)(0) / (float)60);
#endif

      transform_stack_push_rotate_xy(ts, theta, 512.0f, 512.0f);
      transform_stack_concat(ts);

      //////////////////////////
      //
      // define paths
      //
      uint32_t           path_count;
      spn_path_t * const paths = lion_cub_paths(pb, &path_count);

      // this isn't necessary but can start work earlier
      spn(path_builder_flush(pb));
      // spn_context_status(context);

      //////////////////////////
      //
      // define rasters
      //
      uint32_t             raster_count;
      spn_raster_t * const rasters = lion_cub_rasters(rb,          //
                                                      ts,          //
                                                      1,           // rotated copies of lion cub
                                                      paths,       //
                                                      path_count,  //
                                                      &raster_count);
      // this isn't necessary but can start work earlier
      spn(raster_builder_flush(rb));
      // spn_context_status(context);

      //////////////////////////
      //
      // place rasters into composition
      //
      uint32_t             layer_count;
      spn_layer_id * const layer_ids = lion_cub_composition(composition,   //
                                                            rasters,       //
                                                            raster_count,  //
                                                            &layer_count);
      // seal the composition
      spn_composition_seal(composition);
      // spn_context_status(context);

      //////////////////////////
      //
      // (re)define top level styling groups -- normally you wouldn't
      // have to do this but this test resets all Spinel state on each
      // iteration.
      //
      spn_group_id group_id;

      spn(styling_group_alloc(styling, &group_id));

      {
        spn_styling_cmd_t * cmds_enter;

        spn(styling_group_enter(styling, group_id, 1, &cmds_enter));

        cmds_enter[0] = SPN_STYLING_OPCODE_COLOR_ACC_ZERO;
      }

      {
        spn_styling_cmd_t * cmds_leave;

        spn(styling_group_leave(styling, group_id, 4, &cmds_leave));

        float const background[4] = { 1.0f, 1.0f, 1.0f, 1.0f };

        // cmds[0-2]
        spn_styling_background_over_encoder(cmds_leave, background);

        cmds_leave[3] = SPN_STYLING_OPCODE_COLOR_ACC_STORE_TO_SURFACE;
      }

      // this is the root group
      spn(styling_group_parents(styling, group_id, 0, NULL));

      // the range of the root group is maximal [0,layer_count)
      spn(styling_group_range_lo(styling, group_id, 0));
      spn(styling_group_range_hi(styling, group_id, layer_count - 1));

      //
      // add to styling
      //
      lion_cub_styling(styling, group_id, layer_ids, layer_count);

      // seal the styling
      spn_styling_seal(styling);
      // spn_context_status(context);

      //////////////////////////
      //
      // render
      //

      bool const is_first_loop = (ii == 0);
      bool const is_last_loop  = (ii + 1 == loop_count);

      // reset
      rs_image_render.ext = NULL;

      if (is_first_loop)
        {
          // pre-render transition and clear
          rs_image_pre_barrier.ext = rs_image_render.ext;
          rs_pre_image_clear.ext   = &rs_image_pre_barrier;
          rs_image_render.ext      = &rs_pre_image_clear;
        }

      // last?
      if (is_last_loop)
        {
          rs_image_post_copy_to_buffer.ext = rs_image_render.ext;
          rs_image_render.ext              = &rs_image_post_copy_to_buffer;
        }

      spn(render(context, &rs));
      // spn_context_status(context);

      //////////////////////////
      //
      // unseal and reset the composition
      //
      // note that this will block until the render is complete
      //
      spn(composition_unseal(composition));
      spn(composition_reset(composition));

      //////////////////////////
      //
      // unseal and reset the styling
      //
      spn(styling_unseal(styling));
      spn(styling_reset(styling));

      //////////////////////////
      //
      // release paths
      //
      spn(path_release(context, paths, path_count));
      free(paths);

      //////////////////////////
      //
      // release rasters
      //
      spn(raster_release(context, rasters, raster_count));
      free(rasters);

      //
      // free layer ids
      //
      free(layer_ids);

      //
      // drop the top transform
      //
      transform_stack_drop(ts);
    }

  //
  // FIXME(allanmac): need to drain everything before shutting down
  //
  spn_context_status(context);

  //
  // save buffer as PPM
  //
  spn_buffer_to_ppm(surface.h.map, SPN_DEMO_SURFACE_WIDTH, SPN_DEMO_SURFACE_HEIGHT);

  //
  // checksum the buffer
  //
  uint32_t const checksum = spn_buffer_checksum(surface.h.map,           //
                                                SPN_DEMO_SURFACE_WIDTH,  //
                                                SPN_DEMO_SURFACE_HEIGHT);

  uint32_t checksum_good = 0;

  switch (vendor_id)
    {
      case 0x1002:
        checksum_good = SPN_DEMO_LION_CUB_CHECKSUM_AMD;
        break;

      case 0x8086:
        checksum_good = SPN_DEMO_LION_CUB_CHECKSUM_INTEL;
        break;

      case 0x10DE:
        checksum_good = SPN_DEMO_LION_CUB_CHECKSUM_NVIDIA;
        break;
    }

  if (checksum != checksum_good)
    {
      fprintf(stderr, "Image checksum failure: 0x%08X != 0x%08X\n", checksum, checksum_good);

      return EXIT_FAILURE;
    }

  //
  // release the builders, composition and styling
  //
  spn(path_builder_release(pb));
  spn(raster_builder_release(rb));
  spn(composition_release(composition));
  spn(styling_release(styling));

  //
  // release the transform stack
  //
  transform_stack_release(ts);

  //
  // release the context
  //
  spn(context_release(context));

  //
  // - destroy/free sampler, imageview, image, device memory
  //
  vkDestroySampler(environment.d, surface.d.image_info.sampler, environment.ac);

  vkDestroyImageView(environment.d, surface.d.image_info.imageView, environment.ac);

  vkDestroyImage(environment.d, surface.d.image, environment.ac);

  vkFreeMemory(environment.d, surface.d.dm, environment.ac);

  //
  // free mapped buffer
  //
  spn_allocator_device_perm_free(&perm_host_visible, &environment, &surface.h.dbi, surface.h.dm);

  //
  // dispose of allocators
  //
  spn_allocator_device_perm_dispose(&perm_host_visible, &environment);

  //
  // dispose of Vulkan resources
  //
  vk_ok(vk_pipeline_cache_destroy(environment.d,
                                  NULL,
                                  VK_PIPELINE_CACHE_PREFIX_STRING "vk_cache",
                                  environment.pc));

  vkDestroyDevice(environment.d, NULL);

#ifdef SPN_VK_VALIDATION
  vkDestroyDebugReportCallbackEXT(instance, drc, NULL);
#endif

  vkDestroyInstance(instance, NULL);

  return EXIT_SUCCESS;
}

//
//
//
