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

void
fxt_spinel_vk_render::SetUp()
{
  //
  //
  //
  fxt_spinel_vk::SetUp();

  //
  // get the value param
  //
  param_spinel_vk_render const param = GetParam();

  //
  // define device image usage and host image bits
  //
  VkImageUsageFlags const image_d_buf = VK_IMAGE_USAGE_STORAGE_BIT |       //
                                        VK_IMAGE_USAGE_TRANSFER_SRC_BIT |  //
                                        VK_IMAGE_USAGE_TRANSFER_DST_BIT;

  VkBufferUsageFlags const image_h_buf = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |  //
                                         VK_BUFFER_USAGE_TRANSFER_DST_BIT;

  VkMemoryPropertyFlags const image_h_mpf = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |  //
                                            VK_MEMORY_PROPERTY_HOST_CACHED_BIT;

  //
  // create the image
  //
  VkImageCreateInfo const image_ci = {
    .sType     = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
    .pNext     = NULL,
    .flags     = 0,
    .imageType = VK_IMAGE_TYPE_2D,
    .format    = VK_FORMAT_R8G8B8A8_UNORM,
    .extent    = { .width = param.surface.width, .height = param.surface.height, .depth = 1 },
    .mipLevels = 1,

    .arrayLayers           = 1,
    .samples               = VK_SAMPLE_COUNT_1_BIT,
    .tiling                = VK_IMAGE_TILING_OPTIMAL,
    .usage                 = image_d_buf,
    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices   = NULL,
    .initialLayout         = VK_IMAGE_LAYOUT_UNDEFINED
  };

  vk(CreateImage(shared_env->device->vk.d, &image_ci, NULL, &surface.d.image));

  //
  // allocate memory
  //
  VkMemoryRequirements image_mr;

  vkGetImageMemoryRequirements(shared_env->device->vk.d, surface.d.image, &image_mr);

  {
    struct VkMemoryAllocateInfo const mai = {

      .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext           = NULL,
      .allocationSize  = image_mr.size,
      .memoryTypeIndex = vk_find_mem_type_idx(&shared_env->instance->vk.pdmp,
                                              image_mr.memoryTypeBits,
                                              VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };

    vk(AllocateMemory(shared_env->device->vk.d, &mai, NULL, &surface.d.dm));

    vk(BindImageMemory(shared_env->device->vk.d, surface.d.image, surface.d.dm, 0));
  }

  //
  // create sampler
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

  vk(CreateSampler(shared_env->device->vk.d, &sci, NULL, &surface.d.image_info.sampler));

  //
  // create image view
  //
  VkImageViewCreateInfo const image_vci = {

    .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
    .pNext    = NULL,
    .flags    = 0,
    .image    = surface.d.image,
    .viewType = VK_IMAGE_VIEW_TYPE_2D,
    .format   = image_ci.format,

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

  vk(CreateImageView(shared_env->device->vk.d, &image_vci, NULL, &surface.d.image_info.imageView));

  //
  // set initial image layout
  //
  surface.d.image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

  //
  // allocate host-visible memory
  //
  VkBufferCreateInfo const buffer_ci = {

    .sType                 = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
    .pNext                 = NULL,
    .flags                 = 0,
    .size                  = image_mr.size,
    .usage                 = image_h_buf,
    .sharingMode           = VK_SHARING_MODE_EXCLUSIVE,
    .queueFamilyIndexCount = 0,
    .pQueueFamilyIndices   = NULL
  };

  vk(CreateBuffer(shared_env->device->vk.d, &buffer_ci, NULL, &surface.h.dbi.buffer));

  VkMemoryRequirements buffer_mr;

  vkGetBufferMemoryRequirements(shared_env->device->vk.d, surface.h.dbi.buffer, &buffer_mr);

  surface.h.dbi.offset = 0;
  surface.h.dbi.range  = buffer_mr.size;

  {
    struct VkMemoryAllocateInfo const mai = {

      .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
      .pNext           = NULL,
      .allocationSize  = buffer_mr.size,
      .memoryTypeIndex = vk_find_mem_type_idx(&shared_env->instance->vk.pdmp,  //
                                              buffer_mr.memoryTypeBits,
                                              image_h_mpf)
    };

    vk(AllocateMemory(shared_env->device->vk.d, &mai, NULL, &surface.h.dm));

    vk(BindBufferMemory(shared_env->device->vk.d, surface.h.dbi.buffer, surface.h.dm, 0));
  }

  //
  // map host-visible memory
  //
  vk(MapMemory(shared_env->device->vk.d,
               surface.h.dm,
               0,
               VK_WHOLE_SIZE,
               0,
               (void **)&surface.h.map));
}

//
//
//

void
fxt_spinel_vk_render::TearDown()
{
  //
  // free sampler, imageview, memory and image
  //
  vkDestroySampler(shared_env->device->vk.d, surface.d.image_info.sampler, NULL);
  vkDestroyImageView(shared_env->device->vk.d, surface.d.image_info.imageView, NULL);
  vkFreeMemory(shared_env->device->vk.d, surface.d.dm, NULL);
  vkDestroyImage(shared_env->device->vk.d, surface.d.image, NULL);

  //
  // free mapped memory and buffer
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
  VkMappedMemoryRange const mmr = { .sType  = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
                                    .pNext  = NULL,
                                    .memory = surface.h.dm,
                                    .offset = surface.h.dbi.offset,
                                    .size   = surface.h.dbi.range };

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

  uint32_t checksum = 0;

  for (uint32_t ii = 0; ii < pixel_count; ii++)
    {
      checksum += pixels[ii] & 0xFFFFFF;  // alpha channel is ignored
    }

  bool const is_checksum_match = (checksum == param.checksum);

  EXPECT_TRUE(is_checksum_match) << "Checksum mismatch:"  //
                                 << std::endl
                                 << std::uppercase  //
                                 << "  calculated: " << std::hex << checksum << std::endl
                                 << "  expected:   " << std::hex << param.checksum << std::endl;

  if (!is_checksum_match)
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

      ADD_FAILURE() << "Saving surface to: " << filename << std::endl;

      //
      // save PPM to local /tmp
      //
      FILE * file = fopen(filename.c_str(), "wb");

      ASSERT_NE(file, nullptr);

      fprintf(file, "P6\n%u %u\n255\n", param.surface.width, param.surface.height);

      struct spn_color
      {
        uint8_t r;
        uint8_t g;
        uint8_t b;
        uint8_t a;
      } const * const rgba = (struct spn_color *)surface.h.map;

      for (uint32_t ii = 0; ii < pixel_count; ii++)
        {
          ASSERT_EQ(fwrite(rgba + ii, 1, 3, file), 3u);
        }

      fclose(file);
    }
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
// a simple submitter implementation
//

void
fxt_spinel_vk_render::spn_render_submitter(VkQueue               queue,
                                           VkFence               fence,
                                           VkCommandBuffer const cb,
                                           void *                data)
{
  struct VkSubmitInfo const si = { .sType              = VK_STRUCTURE_TYPE_SUBMIT_INFO,
                                   .commandBufferCount = 1,
                                   .pCommandBuffers    = &cb };

  vk(QueueSubmit(queue, 1, &si, fence));
}
