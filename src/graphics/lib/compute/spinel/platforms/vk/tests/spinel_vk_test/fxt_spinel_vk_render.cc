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
  // get the param
  //
  param_spinel_vk_render const param = GetParam();

  //
  // create the test before we proceed
  //
  param.test->create();

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

  //
  // create a transform stack
  //
  struct transform_stack * const ts = transform_stack_create(16);

  transform_stack_push_scale(ts, 32.0f, 32.0f);

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

  spn(composition_set_clip(composition, param.clip.composition));

  //
  // create styling
  //
  spn_styling_t styling;

  uint32_t const layer_count = param.test->layer_count();

  // 16 cmds per layer is conservative plus 7 for a group at depth one
  spn(styling_create(context, &styling, layer_count, layer_count * 16 + 7));
  //
  // set up rendering extensions
  //
  VkBufferImageCopy const bic = {

    .bufferOffset      = 0,
    .bufferRowLength   = param.surface.width,
    .bufferImageHeight = param.surface.height,

    .imageSubresource = { .aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT,
                          .mipLevel       = 0,
                          .baseArrayLayer = 0,
                          .layerCount     = 1 },

    .imageOffset = { .x = 0, .y = 0, .z = 0 },
    .imageExtent = { .width  = param.surface.width,  //
                     .height = param.surface.height,
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
    .submitter_pfn  = fxt_spinel_vk_render::spn_render_submitter,
    .submitter_data = NULL
  };

  spn_render_submit_t const rs = {

    .ext         = &rs_image_render,
    .styling     = styling,
    .composition = composition,
    .clip        = { param.clip.render[0],  // clang-format off
                     param.clip.render[1],
                     param.clip.render[2],
                     param.clip.render[3] }  // clang-format on
  };

  //
  // loop over the entire pipeline
  //
  for (uint32_t ii = 0; ii < param.loops; ii++)
    {
      // define paths
      param.test->paths_create(pb);

      // optional! this isn't required but can start work earlier
      spn(path_builder_flush(pb));

      // define rasters
      param.test->rasters_create(rb, ts);

      // optional! this isn't required but can start work earlier
      spn(raster_builder_flush(rb));

      // define styling and place rasters in composition
      param.test->layers_create(composition, styling, param.is_srgb);

      // explicitly seal the composition
      spn_composition_seal(composition);

      // explicitly seal the styling
      spn_styling_seal(styling);

      //
      // render
      //
      bool const is_first_loop = (ii == 0);
      bool const is_last_loop  = (ii + 1 == param.loops);

      // reset
      rs_image_render.ext = NULL;

      if (is_first_loop && is_last_loop)
        {
          rs_image_pre_barrier.ext         = rs_image_render.ext;      // pre-render transition
          rs_pre_image_clear.ext           = &rs_image_pre_barrier;    // pre-render clear
          rs_image_post_copy_to_buffer.ext = &rs_pre_image_clear.ext;  // post-render copy
          rs_image_render.ext              = &rs_image_post_copy_to_buffer;
        }
      else if (is_first_loop)
        {
          rs_image_pre_barrier.ext = rs_image_render.ext;    // pre-render transition
          rs_pre_image_clear.ext   = &rs_image_pre_barrier;  // pre-render clear
          rs_image_render.ext      = &rs_pre_image_clear;    // render
        }
      else if (is_last_loop)
        {
          rs_image_post_copy_to_buffer.ext = rs_image_render.ext;  // post-render-copy
          rs_image_render.ext              = &rs_image_post_copy_to_buffer;
        }

      spn(render(context, &rs));

      // unseal and reset composition
      spn(composition_unseal(composition));
      spn(composition_reset(composition));

      // unseal and reset styling
      spn(styling_unseal(styling));
      spn(styling_reset(styling));

      // release paths
      param.test->paths_dispose(context);

      // release rasters
      param.test->rasters_dispose(context);
    }

  //
  // wait for asynchronous path/raster releases to complete
  //
  spn(vk_context_wait(context, 0, NULL, true, UINT64_MAX, NULL));

  //
  // checksum?
  //
  checksum();

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

  uint32_t calculated = 0;

  for (uint32_t ii = 0; ii < pixel_count; ii++)
    {
      calculated += pixels[ii] & 0xFFFFFF;  // alpha channel is ignored
    }

  //
  // search for a matching checksum
  //
  bool is_pd_found = true;

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
                  // not empty -- search for a matching device id
                  auto device_id = vendor_id->second.find(shared_env->instance->vk.pdp.deviceID);

                  if (device_id == vendor_id->second.end())
                    {
                      is_pd_found = false;
                    }
                }
            }
          else
            {
              is_pd_found = false;
            }
        }
    }
  else
    {
      is_pd_found = false;
    }

  if (!is_pd_found)
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
      << "(x1:" << render.clip.composition[0]
      << ",y1:" << render.clip.composition[1]
      << ",x2:" << render.clip.composition[2]
      << ",y2:" << render.clip.composition[3]
      << ")"
      << std::endl;

  *os << "clip.render:      "
      << "(x1:" << render.clip.render[0]
      << ",y1:" << render.clip.render[1]
      << ",x2:" << render.clip.render[2]
      << ",y2:" << render.clip.render[3]
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
