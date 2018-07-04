// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/escher/impl/gaussian_3x3f16.h"

#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/impl/image_cache.h"
#include "lib/escher/vk/buffer.h"
#include "lib/escher/vk/texture.h"

namespace {

constexpr uint32_t kGroupSizeX = 16;
constexpr uint32_t kGroupSizeY = 16;

constexpr char kShaderCode[] = R"GLSL(
#version 450
#extension GL_ARB_separate_shader_objects : enable

const vec3 kWeight = vec3(0.27901, 0.44198, 0.27901);
const int kGroupSizeX = 16;
const int kGroupSizeY = 16;
const int kInnerSizeX = 8;
const int kInnerSizeY = 1;

layout (local_size_x = kGroupSizeX/kInnerSizeX,
        local_size_y = kGroupSizeY/kInnerSizeY) in;
layout(binding = 0, rgba16f) uniform image2D input_image;
layout(binding = 1, rgba16f) uniform image2D output_image;

shared vec4 tile[kGroupSizeY][kGroupSizeX];

vec4 tileLoad(ivec2 pos) {
  ivec2 safe_pos = clamp(
    pos, ivec2(0, 0), ivec2(kGroupSizeX-1, kGroupSizeY-1));
  return tile[safe_pos.y][safe_pos.x];
}

void main() {
  ivec2 global_anchor = ivec2(gl_GlobalInvocationID.x * kInnerSizeX,
                              gl_GlobalInvocationID.y * kInnerSizeY);
  ivec2 local_anchor = ivec2(gl_LocalInvocationID.x * kInnerSizeX,
                             gl_LocalInvocationID.y * kInnerSizeY);
  for (int dy = 0; dy < kInnerSizeY; dy++) {
    for (int dx = 0; dx < kInnerSizeX; dx++) {
      ivec2 global_pos = ivec2(global_anchor.x+dx, global_anchor.y+dy);
      ivec2 local_pos = ivec2(local_anchor.x+dx, local_anchor.y+dy);
      vec4 left = imageLoad(input_image, ivec2(global_pos.x-1, global_pos.y));
      vec4 mid = imageLoad(input_image, global_pos);
      vec4 right = imageLoad(input_image, ivec2(global_pos.x+1, global_pos.y));
      tile[local_pos.y][local_pos.x] =
        kWeight.x * left + kWeight.y * mid + kWeight.z * right;
    }
  }
  // Guarantees `tile` is computed.
  barrier();
  // Guarantees `tile` is coherent across threads.
  groupMemoryBarrier();
  for (int dy = 0; dy < kInnerSizeY; dy++) {
    for (int dx = 0; dx < kInnerSizeX; dx++) {
      ivec2 global_pos = ivec2(global_anchor.x+dx, global_anchor.y+dy);
      ivec2 local_pos = ivec2(local_anchor.x+dx, local_anchor.y+dy);
      vec4 top = tileLoad(ivec2(local_pos.x, local_pos.y-1));
      vec4 mid = tileLoad(local_pos);
      vec4 bottom = tileLoad(ivec2(local_pos.x, local_pos.y+1));
      vec4 result =
        kWeight.x * top + kWeight.y * mid + kWeight.z * bottom;
      imageStore(output_image, global_pos, result);
    }
  }
}
)GLSL";

}  // namespace

namespace escher {
namespace impl {

Gaussian3x3f16::Gaussian3x3f16(EscherWeakPtr escher)
    : escher_(std::move(escher)) {}

void Gaussian3x3f16::Apply(CommandBuffer* command_buffer,
                           const TexturePtr& input, const TexturePtr& output) {
  if (!kernel_) {
    kernel_ = std::make_unique<ComputeShader>(
        escher_, std::vector<vk::ImageLayout>{vk::ImageLayout::eGeneral,
                                              vk::ImageLayout::eGeneral},
        std::vector<vk::DescriptorType>{},
        /* push_constants_size= */ 0, kShaderCode);
  }
  kernel_->Dispatch({input, output}, {}, command_buffer,
                    (input->width() + kGroupSizeX - 1) / kGroupSizeX,
                    (input->height() + kGroupSizeY - 1) / kGroupSizeY,
                    /* z= */ 1, /* push_constants= */ nullptr);
}

}  // namespace impl
}  // namespace escher
