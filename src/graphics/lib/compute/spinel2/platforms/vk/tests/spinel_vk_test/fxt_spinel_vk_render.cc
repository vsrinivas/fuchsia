// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fxt_spinel_vk_render.h"

#include "common/vk/find_mem_type_idx.h"

//
//
//

using namespace spinel::vk::test;

//
//
//

static void
vk_alloc_dbi_dm(VkPhysicalDevice         pd,
                VkDevice                 d,
                VkDeviceSize             size,
                VkBufferUsageFlags       buf,
                VkMemoryPropertyFlags    mpf,
                VkDescriptorBufferInfo * dbi,
                VkDeviceMemory *         dm)
{
  //
  // Allocate buffers
  //
  VkBufferCreateInfo bci = {

    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext                 = NULL,
    .flags                 = 0,
    .size                  = size,
    .usage                 = buf,
    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices   = NULL
  };

  vk(CreateBuffer(d, &bci, NULL, &dbi->buffer));

  //
  //
  //
  VkMemoryRequirements mr;

  vkGetBufferMemoryRequirements(d, dbi->buffer, &mr);

  dbi->offset = 0;
  dbi->range  = mr.size;

  //
  // Physical device memory properties are only used here.
  //
  VkPhysicalDeviceMemoryProperties pdmp;

  vkGetPhysicalDeviceMemoryProperties(pd, &pdmp);

  //
  // Indicate that we're going to get the buffer's address
  //
  VkMemoryAllocateFlagsInfo const mafi = {

    .sType      = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
    .pNext      = NULL,
    .flags      = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
    .deviceMask = 0
  };

  VkMemoryAllocateFlagsInfo const * const mafi_next =
    (buf & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT)  //
      ? &mafi
      : NULL;

  struct VkMemoryAllocateInfo const mai = {

    .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
    .pNext           = mafi_next,
    .allocationSize  = mr.size,
    .memoryTypeIndex = vk_find_mem_type_idx(&pdmp,  //
                                            mr.memoryTypeBits,
                                            mpf)
  };

  vk(AllocateMemory(d, &mai, NULL, dm));

  vk(BindBufferMemory(d, dbi->buffer, *dm, 0));
}

//
//
//

void
fxt_spinel_vk_render::SetUp()
{
  //
  //
  //
  fxt_spinel_vk::SetUp();

  //
  // get the param
  //
  param_spinel_vk_render const param = GetParam();

  //
  // create the test before we proceed
  //
  param.test->create();

  //
  // copy the swapchain to a host buffer
  //
  VkBufferUsageFlags const surf_h_buf = {

    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  // HOST
      VK_BUFFER_USAGE_TRANSFER_DST_BIT,   //
  };

  VkMemoryPropertyFlags const surf_h_mpf = {

    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |  // HOST
      VK_MEMORY_PROPERTY_HOST_CACHED_BIT,  //
  };

  //
  // FIXME(allanmac): The Spinel target should vend available surface format(s)
  // like BGRA32 and FP16x4.
  //
  VkDeviceSize const surf_texel_size = sizeof(uint32_t);
  VkDeviceSize const surf_size       = param.surface.width * param.surface.height * surf_texel_size;

  //
  // Host
  //
  vk_alloc_dbi_dm(shared_env->instance->vk.pd,
                  shared_env->device->vk.d,
                  surf_size,
                  surf_h_buf,
                  surf_h_mpf,
                  &surface.h.dbi,
                  &surface.h.dm);

  //
  // Map host-visible memory
  //
  vk(MapMemory(shared_env->device->vk.d,
               surface.h.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&surface.h.map));

  //
  // Get global transform from Spinel context
  //
  spinel_context_limits_t limits;

  spinel(context_get_limits(context, &limits));

  //
  // create a transform stack
  //
  struct spinel_transform_stack * const ts = spinel_transform_stack_create(16);

  //
  // Apply world space transform (reflect over y=x at subpixel resolution)
  //
  spinel_transform_stack_push_transform(ts, &limits.global_transform);

  //
  // create builders
  //
  spinel_path_builder_t pb;

  spinel(path_builder_create(context, &pb));

  spinel_raster_builder_t rb;

  spinel(raster_builder_create(context, &rb));

  //
  // create composition
  //
  spinel_composition_t composition;

  spinel(composition_create(context, &composition));

  spinel(composition_set_clip(composition, &param.clip.composition));

  //
  // create styling
  //
  // 16 cmds per layer is conservative plus 7 for a group at depth one
  //
  uint32_t const layer_count = param.test->layer_count();

  spinel_styling_create_info_t const styling_create_info = {
    .layer_count = layer_count,
    .cmd_count   = layer_count * 16 + 7,
  };

  spinel_styling_t styling;

  spinel(styling_create(context, &styling_create_info, &styling));

  //
  // create swapchain
  //
  spinel_swapchain_create_info_t const swapchain_create_info = {
    .extent = param.surface,
    .count  = 1,
  };

  spinel_swapchain_t swapchain;

  spinel(swapchain_create(context, &swapchain_create_info, &swapchain));

  //
  // set up rendering extensions
  //
  spinel_vk_swapchain_submit_ext_compute_fill_t const compute_fill = {
    .ext   = NULL,
    .type  = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_FILL,
    .dword = 0xFFFFFFFF,
  };

  spinel_vk_swapchain_submit_ext_compute_copy_t const compute_copy = {
    .ext  = (void *)&compute_fill,
    .type = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_COPY,
    .dst  = surface.h.dbi,
  };

  spinel_vk_swapchain_submit_ext_compute_render_t const compute_render = {
    .ext          = (void *)&compute_copy,
    .type         = SPN_VK_SWAPCHAIN_SUBMIT_EXT_TYPE_COMPUTE_RENDER,
    .clip         = param.clip.render,
    .extent_index = 0,
  };

  spinel_swapchain_submit_t const swapchain_submit = {
    .ext         = (void *)&compute_render,
    .styling     = styling,
    .composition = composition,
  };

  //
  // loop over the entire pipeline
  //
  for (uint32_t ii = 0; ii < param.loops; ii++)
    {
      // define paths -- note that an optional flush is invoked
      param.test->paths_create(pb);

      // define rasters -- note that an optional flush is invoked
      param.test->rasters_create(rb, ts);

      // define styling and place rasters in composition -- flushes occur
      param.test->layers_create(composition, styling, param.is_srgb);

      // render
      spinel(swapchain_submit(swapchain, &swapchain_submit));

      // unseal and reset composition
      spinel(composition_unseal(composition));
      spinel(composition_reset(composition));

      // unseal and reset styling
      spinel(styling_unseal(styling));
      spinel(styling_reset(styling));

      // release paths
      param.test->paths_dispose(context);

      // release rasters
      param.test->rasters_dispose(context);
    }

  //
  // checksum?
  //
  checksum();

  //
  // release the builders, composition and styling
  //
  spinel(path_builder_release(pb));
  spinel(raster_builder_release(rb));
  spinel(composition_release(composition));
  spinel(styling_release(styling));
  spinel(swapchain_release(swapchain));

  //
  // release the transform stack
  //
  spinel_transform_stack_release(ts);

  //
  // dispose of the param
  //
  param.test->dispose();
}

//
//
//

void
fxt_spinel_vk_render::TearDown()
{
  //
  // free host and device surfaces
  //
  vkFreeMemory(shared_env->device->vk.d, surface.h.dm, NULL);
  vkDestroyBuffer(shared_env->device->vk.d, surface.h.dbi.buffer, NULL);

  //
  //
  //
  fxt_spinel_vk::TearDown();
}

//
//
//

void
fxt_spinel_vk_render::checksum()
{
  //
  // invalidate the mapped memory
  //
  VkMappedMemoryRange const mmr = {

    .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
    .pNext  = NULL,
    .memory = surface.h.dm,
    .offset = surface.h.dbi.offset,
    .size   = surface.h.dbi.range
  };

  vk(InvalidateMappedMemoryRanges(shared_env->device->vk.d, 1, &mmr));

  //
  // FIXME(allanmac): this implementation is OK for now since we're
  // checksumming per device.  Note that changing the size of the
  // surface impacts the checksum.
  //
  // NOTE(allanmac): for now it's assumed that copying source image to
  // the destination buffer results in a packed / non-strided array of
  // pixels.
  //
  param_spinel_vk_render const param = GetParam();

  uint32_t const   pixel_count = param.surface.width * param.surface.height;
  uint32_t const * pixels      = (uint32_t const *)surface.h.map;

  uint32_t calculated = 0;

  for (uint32_t ii = 0; ii < pixel_count; ii++)
    {
      calculated += pixels[ii] & 0xFFFFFF;  // alpha channel is ignored
    }

  //
  // search for a matching { Platform x Device x Checksum }
  //
  bool is_pdc_found = true;

  auto const checksum = param.checksums.find(calculated);

  // checksum found?
  if (checksum != param.checksums.end())
    {
      // an empty set of physical devices implies all match
      if (!checksum->second.empty())
        {
          // not empty -- search for a matching vendor id
          auto vendor_id = checksum->second.find(shared_env->instance->vk.pdp.vendorID);

          // match is found
          if (vendor_id != checksum->second.end())
            {
              // an empty set of device ids implies all match
              if (!vendor_id->second.empty())
                {
                  is_pdc_found = false;

                  auto map = vendor_id->second;

                  for (auto pair = map.cbegin(); pair != map.cend(); pair++)
                    {
                      if ((shared_env->instance->vk.pdp.deviceID >= pair->first) &&
                          (shared_env->instance->vk.pdp.deviceID <= pair->second))
                        {
                          is_pdc_found = true;
                          break;
                        }
                    }
                }
            }
          else
            {
              is_pdc_found = false;
            }
        }
    }
  else
    {
      is_pdc_found = false;
    }

  if (!is_pdc_found)
    {
      ::testing::TestInfo const * const test_info =
        ::testing::UnitTest::GetInstance()->current_test_info();

      std::string filename = "/tmp/surface_";

      filename += test_info->test_suite_name();
      filename += "_";
      filename += test_info->name();
      filename += ".ppm";

      //
      // gtest naming uses '/' separators
      //
      // NOTE(allanmac): if we want to dump the surface PPM into a
      // hierarchy of directories then don't replace the '/' separator
      // and ensure any intermediate directories are created.
      //
      std::replace(filename.begin() + std::strlen("/tmp/surface_"), filename.end(), '/', '_');

      ADD_FAILURE() << std::hex                               //
                    << std::uppercase                         //
                    << "Checksum '0x"                         //
                    << calculated                             //
                    << "' not found for physical device {"    //
                    << shared_env->instance->vk.pdp.vendorID  //
                    << ":"                                    //
                    << shared_env->instance->vk.pdp.deviceID  //
                    << "}"                                    //
                    << std::endl                              //
                    << "Saving surface to: "                  //
                    << filename;

      //
      // save PPM to local /tmp
      //
      FILE * file = fopen(filename.c_str(), "wb");

      ASSERT_NE(file, nullptr);

      fprintf(file, "P6\n%u %u\n255\n", param.surface.width, param.surface.height);

      struct spinel_color
      {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
      } const * const rgba = (struct spinel_color *)surface.h.map;

      for (uint32_t ii = 0; ii < pixel_count; ii++)
        {
          ASSERT_EQ(fwrite(rgba + ii, 1, 3, file), 3u);
        }

      fclose(file);
    }
}

void ::spinel::vk::test::PrintTo(const param_spinel_vk_render & render, std::ostream * os)
{
  // clang-format off
  *os << std::endl;

  *os << "-----------------" << std::endl;

  *os << "name:             \""
      << (render.name ? render.name : "<NULL>")
      << "\""
      << std::endl;

  *os << "surface:          "
      << "(w:" << render.surface.width
      << ",h:" << render.surface.height
      << ")"
      << std::endl;

  *os << "clip.composition: "
      << "(x0:" << render.clip.composition.x0
      << ",y0:" << render.clip.composition.y0
      << ",x1:" << render.clip.composition.x1
      << ",y1:" << render.clip.composition.y1
      << ")"
      << std::endl;

  *os << "clip.render:      "
      << "(x0:" << render.clip.render.x0
      << ",y0:" << render.clip.render.y0
      << ",x1:" << render.clip.render.x1
      << ",y1:" << render.clip.render.y1
      << ")"
      << std::endl;

  *os << "loops:            "
      << render.loops
      << std::endl;

  *os << "checksums:        "
      << std::showbase
      << std::hex
      << std::uppercase;
  // dump checksum maps in uppercase hex
  ::testing::internal::UniversalTersePrint(render.checksums, os);
  *os << std::endl;

  *os << "-----------------"
      << std::endl;
  // clang-format off
}

//
// param name suffix generator
//
std::string
fxt_spinel_vk_render::param_name(testing::TestParamInfo<param_spinel_vk_render> const & info)
{
  return std::string(info.param.name);
}

//
//
//
