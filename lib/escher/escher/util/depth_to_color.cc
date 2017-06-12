// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/util/depth_to_color.h"

#include "escher/impl/image_cache.h"
#include "escher/impl/command_buffer.h"
#include "escher/renderer/texture.h"
#include "escher/renderer/timestamper.h"
#include "escher/resources/resource_life_preserver.h"

namespace {
constexpr char g_kernel_src[] = R"GLSL(
  #version 450
  #extension GL_ARB_separate_shader_objects : enable

  layout (binding = 0) uniform sampler2D depthImage;
  layout (binding = 1, rgba8) uniform image2D resultImage;

  const int kSize = 8;

  void main() {
    ivec2 base = ivec2(gl_GlobalInvocationID.xy) * kSize;
    for (int x = base.x; x < base.x + kSize; ++x) {
      for (int y = base.y; y < base.y + kSize; ++y) {
        float depth = texture(depthImage, ivec2(x, y)).r;
        imageStore(resultImage, ivec2(x, y), vec4(depth, depth, depth, 1.0));
      }
    }
  }
  )GLSL";
}

namespace escher {

DepthToColor::DepthToColor(impl::GlslToSpirvCompiler* compiler,
                           impl::ImageCache* image_cache,
                           ResourceLifePreserver* life_preserver)
    : compiler_(compiler),
      image_cache_(image_cache),
      life_preserver_(life_preserver) {}

TexturePtr DepthToColor::Convert(impl::CommandBuffer* command_buffer,
                                 const TexturePtr& depth_texture,
                                 vk::ImageUsageFlags image_flags,
                                 Timestamper* timestamper) {
  uint32_t width = depth_texture->width();
  uint32_t height = depth_texture->height();

  // Size of neighborhood of pixels to work on for each invocation of the
  // compute kernel.  Must match the value in the compute shader source code,
  // and be a multiple of 4.
  constexpr uint32_t kSize = 8;

  uint32_t work_groups_x = width / kSize + (width % kSize > 0 ? 1 : 0);
  uint32_t work_groups_y = height / kSize + (height % kSize > 0 ? 1 : 0);

  ImagePtr tmp_image =
      image_cache_->NewImage({vk::Format::eR8G8B8A8Unorm, width, width, 1,
                              image_flags | vk::ImageUsageFlagBits::eStorage});
  TexturePtr tmp_texture = ftl::MakeRefCounted<Texture>(
      life_preserver_, tmp_image, vk::Filter::eNearest,
      vk::ImageAspectFlagBits::eColor, true);
  command_buffer->TransitionImageLayout(tmp_image, vk::ImageLayout::eUndefined,
                                        vk::ImageLayout::eGeneral);

  if (!kernel_) {
    FTL_DLOG(INFO) << "DepthToColor: Lazily instantiating kernel.";
    kernel_ = std::make_unique<impl::ComputeShader>(
        image_cache_->vulkan_context(),
        std::vector<vk::ImageLayout>{vk::ImageLayout::eShaderReadOnlyOptimal,
                                     vk::ImageLayout::eGeneral},
        0, g_kernel_src, compiler_);
  }

  kernel_->Dispatch({depth_texture, tmp_texture}, command_buffer, work_groups_x,
                    work_groups_y, 1, nullptr);

  timestamper->AddTimestamp("converted depth image to color image");
  return tmp_texture;
}

}  // namespace escher
