// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/util/depth_to_color.h"

#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/renderer/frame.h"
#include "lib/escher/vk/buffer.h"
#include "lib/escher/vk/image_factory.h"
#include "lib/escher/vk/texture.h"

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

DepthToColor::DepthToColor(EscherWeakPtr escher, ImageFactory* image_factory)
    : escher_(std::move(escher)), image_factory_(image_factory) {}

TexturePtr DepthToColor::Convert(const FramePtr& frame,
                                 const TexturePtr& depth_texture,
                                 vk::ImageUsageFlags image_flags) {
  auto command_buffer = frame->command_buffer();
  uint32_t width = depth_texture->width();
  uint32_t height = depth_texture->height();

  // Size of neighborhood of pixels to work on for each invocation of the
  // compute kernel.  Must match the value in the compute shader source code,
  // and be a multiple of 4.
  constexpr uint32_t kSize = 8;

  uint32_t work_groups_x = width / kSize + (width % kSize > 0 ? 1 : 0);
  uint32_t work_groups_y = height / kSize + (height % kSize > 0 ? 1 : 0);

  ImagePtr tmp_image = image_factory_->NewImage(
      {vk::Format::eR8G8B8A8Unorm, width, width, 1,
       image_flags | vk::ImageUsageFlagBits::eStorage});
  TexturePtr tmp_texture = fxl::MakeRefCounted<Texture>(
      escher_->resource_recycler(), tmp_image, vk::Filter::eNearest,
      vk::ImageAspectFlagBits::eColor, true);
  command_buffer->TransitionImageLayout(tmp_image, vk::ImageLayout::eUndefined,
                                        vk::ImageLayout::eGeneral);

  if (!kernel_) {
    FXL_DLOG(INFO) << "DepthToColor: Lazily instantiating kernel.";
    kernel_ = std::make_unique<impl::ComputeShader>(
        escher_,
        std::vector<vk::ImageLayout>{vk::ImageLayout::eShaderReadOnlyOptimal,
                                     vk::ImageLayout::eGeneral},
        std::vector<vk::DescriptorType>{}, 0, g_kernel_src);
  }

  kernel_->Dispatch({depth_texture, tmp_texture}, {}, command_buffer,
                    work_groups_x, work_groups_y, 1, nullptr);

  frame->AddTimestamp("converted depth image to color image");
  return tmp_texture;
}

}  // namespace escher
