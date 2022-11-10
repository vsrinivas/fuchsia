// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/escher/flatland/rectangle_compositor.h"

#include "src/ui/lib/escher/impl/naive_image.h"
#include "src/ui/lib/escher/mesh/indexed_triangle_mesh_upload.h"
#include "src/ui/lib/escher/mesh/tessellation.h"
#include "src/ui/lib/escher/renderer/render_funcs.h"
#include "src/ui/lib/escher/util/image_utils.h"
#include "src/ui/lib/escher/vk/shader_program.h"
#include "src/ui/lib/escher/vk/texture.h"

namespace escher {
const vk::ImageUsageFlags RectangleCompositor::kRenderTargetUsageFlags =
    vk::ImageUsageFlagBits::eColorAttachment;
const vk::ImageUsageFlags RectangleCompositor::kTextureUsageFlags =
    vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eSampled;

namespace {

static constexpr uint32_t kTransientTargetAttachmentIndex = 0;
static constexpr uint32_t kOutputTargetAttachmentIndex = 1;

// Helper function which factors out common code from the two InitRenderPassInfo() variants.
static void InitRenderPassInfoHelper(RenderPassInfo* rp,
                                     const RenderPassInfo::AttachmentInfo& transient_info,
                                     const RenderPassInfo::AttachmentInfo& output_info,
                                     const RenderPassInfo::AttachmentInfo& depth_stencil_info) {
  FX_DCHECK(output_info.sample_count == 1);

  rp->color_attachment_infos[kTransientTargetAttachmentIndex] = transient_info;
  rp->color_attachment_infos[kOutputTargetAttachmentIndex] = output_info;
  rp->depth_stencil_attachment_info = depth_stencil_info;

  {
    // We have one transient attachment, and one output attachment.
    rp->num_color_attachments = 2;

    // Clear the intermediate attachment. We don't need to store it.
    rp->clear_attachments = 1u << kTransientTargetAttachmentIndex;

    // Clear and store the output color attachment 1.
    rp->clear_attachments |= 1u << kOutputTargetAttachmentIndex;
    rp->store_attachments |= 1u << kOutputTargetAttachmentIndex;

    // Standard flags for a depth-testing render-pass that needs to first clear
    // the depth image.
    rp->op_flags = RenderPassInfo::kClearDepthStencilOp | RenderPassInfo::kOptimalColorLayoutOp |
                   RenderPassInfo::kOptimalDepthStencilLayoutOp;
    FX_DCHECK(depth_stencil_info.format != vk::Format::eUndefined);

    rp->clear_color[kTransientTargetAttachmentIndex].setFloat32({0.f, 0.f, 0.f, 0.f});
    rp->clear_color[kOutputTargetAttachmentIndex].setFloat32({0.f, 0.f, 0.f, 0.f});
  }

  // This is the subpass used to render the renderables. They render to a
  // transient image.
  RenderPassInfo::Subpass subpass = {.color_attachments = {kTransientTargetAttachmentIndex},
                                     .input_attachments = {},
                                     .resolve_attachments = {},
                                     .num_color_attachments = 1,
                                     .num_input_attachments = 0,
                                     .num_resolve_attachments = 0};

  // This is the subpass used to perform color conversion. The transient attachment
  // from subpass1 becomes the input attachment here, and then this subpass renders
  // out to the render target.
  RenderPassInfo::Subpass subpass2 = {.color_attachments = {kOutputTargetAttachmentIndex},
                                      .input_attachments = {kTransientTargetAttachmentIndex},
                                      .resolve_attachments = {},
                                      .num_color_attachments = 1,
                                      .num_input_attachments = 1,
                                      .num_resolve_attachments = 0};

  rp->subpasses.push_back(subpass);
  rp->subpasses.push_back(subpass2);

  // Make null the remaining color attachment slots we are not using.
  for (size_t i = rp->num_color_attachments; i < VulkanLimits::kNumColorAttachments; ++i) {
    rp->color_attachment_infos[i] = {};
    rp->color_attachments[i] = nullptr;
  }
}

// We need a render pass with two subpasses in order to apply color conversion.
// The first subpass renders each of the renderables to a transient framebuffer,
// and the second subpass reads in those transient values as input, and is used to
// compute color conversion as a post processing effect over the entire output
// framebuffer. Since color-conversion doesn't require knowledge of adjacent
// pixels, subpasses are a relatively straightforward way to handle it.
bool SetupColorConversionDualPass(RenderPassInfo* rp, vk::Rect2D render_area,
                                  const ImagePtr& transient_image, const ImagePtr& output_image,
                                  const TexturePtr& depth_texture) {
  FX_DCHECK(output_image->info().sample_count == 1);
  rp->render_area = render_area;

  RenderPassInfo::AttachmentInfo transient_info, output_info;
  RenderPassInfo::AttachmentInfo depth_stencil_info;
  if (output_image) {
    if (!output_image->is_swapchain_image()) {
      FX_LOGS(ERROR) << "RenderPassInfo::InitRenderPassInfo(): Output image doesn't have valid "
                        "swapchain layout.";
      return false;
    }
    if (output_image->swapchain_layout() != output_image->layout()) {
      FX_LOGS(ERROR) << "RenderPassInfo::InitRenderPassInfo(): Current layout of output image "
                        "does not match its swapchain layout.";
      return false;
    }
    transient_info.InitFromImage(transient_image);
    output_info.InitFromImage(output_image);
  }

  depth_stencil_info.InitFromImage(depth_texture->image());

  InitRenderPassInfoHelper(rp, transient_info, output_info, depth_stencil_info);

  ImageViewPtr transient_image_view = ImageView::New(transient_image);
  ImageViewPtr output_image_view = ImageView::New(output_image);
  rp->color_attachments[kTransientTargetAttachmentIndex] = std::move(transient_image_view);
  rp->color_attachments[kOutputTargetAttachmentIndex] = std::move(output_image_view);
  rp->depth_stencil_attachment = depth_texture;
  return true;
}

vec4 GetPremultipliedRgba(vec4 rgba) { return vec4(vec3(rgba) * rgba.a, rgba.a); }

// Draws a single rectangle at a particular depth value, z.
void DrawSingle(CommandBuffer* cmd_buf, const ShaderProgramPtr& program,
                const Rectangle2D& rectangle, const Texture* texture, const glm::vec4& color,
                float z) {
  TRACE_DURATION("gfx", "RectangleCompositor::DrawSingle");

  // Set the shader program to be used.
  const SamplerPtr& sampler = texture->sampler()->is_immutable() ? texture->sampler() : nullptr;
  cmd_buf->SetShaderProgram(program, sampler);

  // Bind texture to use in the fragment shader.
  cmd_buf->BindTexture(/*set*/ 0, /*binding*/ 0, texture);

  // Struct to store all the push constant data in the vertex shader
  // so we only need to make a single call to PushConstants().
  struct VertexShaderPushConstants {
    alignas(16) vec3 origin;
    alignas(8) vec2 extent;
    alignas(8) std::array<vec2, 4> uvs;
  };

  // Set up the push constants struct with data from the renderable and z value.
  VertexShaderPushConstants constants = {
      .origin = vec3(rectangle.origin, z),
      .extent = rectangle.extent,
      .uvs = rectangle.clockwise_uvs,
  };

  // We offset by 16U to account for the fact that the previous call to
  // PushConstants() for the batch-level bounds was a glm::vec3, which
  // takes up 16 bytes with padding in the vertex shader.
  cmd_buf->PushConstants(constants, /*offset*/ 16U);

  // We make one more call to PushConstants() to push the color to the
  // fragment shader. This is so that the data aligns with the push constant
  // range for the fragment shader only, otherwise it would overlap the ranges
  // for both the vertex and fragment shaders.
  cmd_buf->PushConstants(GetPremultipliedRgba(color), /*offset*/ 80U);

  // In Vulkan, YUV textures don't have a color space defined by the format. The OETF (Opto
  // Electrical Transfer Function) for BT.709 is closely approximated by using power of 2 for the
  // RGB components of the sampled texture in the fragment shader. We make another call to
  // PushConstants() to push this gamma power value.
  cmd_buf->PushConstants(texture->is_yuv_format() ? 2.f : 1.f, /*offset*/ 96U);

  // Draw two triangles. The vertex shader knows how to use the gl_VertexIndex
  // of each vertex to compute the appropriate position and UV values.
  cmd_buf->Draw(/*vertex_count*/ 6);
}

// Renders the batch of provided rectangles using the provided shader program.
// Renderables are separated into opaque and translucent groups. The opaque
// renderables are rendered from front-to-back while the translucent renderables
// are rendered from back-to-front.
void TraverseBatch(CommandBuffer* cmd_buf, vec3 bounds, ShaderProgramPtr program,
                   const std::vector<Rectangle2D>& rectangles,
                   const std::vector<const TexturePtr>& textures,
                   const std::vector<RectangleCompositor::ColorData>& color_data) {
  TRACE_DURATION("gfx", "RectangleCompositor::TraverseBatch");
  int64_t num_renderables = static_cast<int64_t>(rectangles.size());

  // Push the bounds as a constant for all renderables to be used in the vertex shader.
  cmd_buf->PushConstants(bounds);

  // Opaque, front to back.
  {
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kOpaque);
    cmd_buf->SetDepthTestAndWrite(true, true);

    float z = 1.f;
    for (int64_t i = num_renderables - 1; i >= 0; i--) {
      if (color_data[i].is_opaque) {
        DrawSingle(cmd_buf, program, rectangles[i], textures[i].get(), color_data[i].color, z);
      }
      z += 1.f;
    }
  }

  // Translucent, back to front.
  {
    cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kTranslucent);
    cmd_buf->SetDepthTestAndWrite(true, false);
    float z = static_cast<float>(rectangles.size());
    for (int64_t i = 0; i < num_renderables; i++) {
      if (!color_data[i].is_opaque) {
        DrawSingle(cmd_buf, program, rectangles[i], textures[i].get(), color_data[i].color, z);
      }
      z -= 1.f;
    }
  }
}

void ApplyColorConversion(CommandBuffer* cmd_buf, ShaderProgramPtr program,
                          ImageViewPtr input_attachment,
                          const ColorConversionParams& color_conversion_params) {
  TRACE_DURATION("gfx", "RectangleCompositor::ApplyColorConversion");
  cmd_buf->SetToDefaultState(CommandBuffer::DefaultState::kOpaque);
  cmd_buf->SetDepthTestAndWrite(false, false);

  cmd_buf->SetShaderProgram(program, nullptr);

  cmd_buf->BindInputAttachment(/*set*/ 0, /*binding*/ 0, input_attachment);

  cmd_buf->PushConstants(color_conversion_params);

  // Draw one triangle. The vertex shader knows how to use the gl_VertexIndex
  // of each vertex to compute the appropriate position.
  cmd_buf->Draw(/*vertex_count*/ 3);
}

}  // anonymous namespace

// RectangleCompositor constructor. Initializes the shader program and allocates
// GPU buffers to store mesh data.
RectangleCompositor::RectangleCompositor(EscherWeakPtr escher)
    : escher_(escher),
      standard_program_(escher->GetProgram(kFlatlandStandardProgram)),
      color_conversion_program_(escher->GetProgram(kFlatlandColorConversionProgram)) {}

// DrawBatch generates the Vulkan data needed to render the batch (e.g. renderpass,
// bounds, etc) and calls |TraverseBatch| which iterates over the renderables and
// submits them for rendering.
void RectangleCompositor::DrawBatch(CommandBuffer* cmd_buf,
                                    const std::vector<Rectangle2D>& rectangles,
                                    const std::vector<const TexturePtr>& textures,
                                    const std::vector<ColorData>& color_data,
                                    const ImagePtr& output_image, const TexturePtr& depth_buffer,
                                    bool apply_color_conversion) {
  // TODO(fxbug.dev/43278): Add custom clear colors. We could either pass in another parameter to
  // this function or try to embed clear-data into the existing api. For example, one could
  // check to see if the back rectangle is fullscreen and solid-color, in which case we can
  // treat it as a clear instead of rendering it as a renderable.
  FX_CHECK(cmd_buf && output_image && depth_buffer);

  // Inputs need to be the same length.
  FX_CHECK(rectangles.size() == textures.size());
  FX_CHECK(rectangles.size() == color_data.size());

  // Initialize the render pass.
  RenderPassInfo render_pass;
  vk::Rect2D render_area = {{0, 0}, {output_image->width(), output_image->height()}};

  // Construct the bounds that are used in the vertex shader to convert the
  // renderable positions into normalized device coordinates (NDC). The width
  // and height are divided by 2 to pre-optimize the shift that happens in the
  // shader which realigns the NDC coordinates so that (0,0) is in the center
  // instead of in the top-left-hand corner.
  vec3 bounds(static_cast<float>(output_image->width() * 0.5),
              static_cast<float>(output_image->height() * 0.5), rectangles.size());

  // If we don't have any color conversion data, stick to a single subpass.
  if (!apply_color_conversion) {
    // Setup a standard 1-pass renderpass where we render directly into the output image.
    if (!RenderPassInfo::InitRenderPassInfo(&render_pass, render_area, output_image,
                                            depth_buffer)) {
      FX_LOGS(ERROR) << "RectangleCompositor::DrawBatch(): RenderPassInfo initialization failed. "
                        "Exiting.";
      return;
    }

    // Start the render pass.
    cmd_buf->BeginRenderPass(render_pass);

    // Iterate over all the renderables and draw them.
    TraverseBatch(cmd_buf, bounds, standard_program_, rectangles, textures, color_data);

    // End the render pass.
    cmd_buf->EndRenderPass();

  }
  // Here we'll need to setup the dual pass system.
  else {
    auto transient_image = CreateOrFindTransientImage(output_image);

    // Setup a 2-pass render pass where we first render into an intermediate buffer (not really:
    // we try to use a transient buffer to avoid flushing memory from GPU caches to GPU-external
    // memory) and then use that as an input attachment for the output pass, where we finally
    // apply color correction.
    if (!SetupColorConversionDualPass(&render_pass, render_area, transient_image, output_image,
                                      depth_buffer)) {
      FX_LOGS(ERROR) << "RectangleCompositor::DrawBatch(): RenderPassInfo initialization failed. "
                        "Exiting.";
      return;
    }

    if (transient_image->layout() != vk::ImageLayout::eColorAttachmentOptimal) {
      cmd_buf->impl()->TransitionImageLayout(transient_image, vk::ImageLayout::eUndefined,
                                             vk::ImageLayout::eColorAttachmentOptimal);
    }

    // Start the render pass.
    cmd_buf->BeginRenderPass(render_pass);

    // Iterate over all the renderables and draw them.
    TraverseBatch(cmd_buf, bounds, standard_program_, rectangles, textures, color_data);

    cmd_buf->NextSubpass();

    ApplyColorConversion(cmd_buf, color_conversion_program_,
                         render_pass.color_attachments[kTransientTargetAttachmentIndex],
                         color_conversion_params_);

    // End the render pass.
    cmd_buf->EndRenderPass();
  }
}

// TODO(fxbug.dev/94252): It doesn't seem like all platforms actually support transient images.
// So this is going to be a regular image for now.
ImagePtr RectangleCompositor::CreateOrFindTransientImage(const ImagePtr& image) {
  auto itr = transient_image_map_.find(image->info());
  if (itr != transient_image_map_.end()) {
    return itr->second;
  }

  ImageInfo info;
  info.format = image->info().format;
  info.width = image->info().width;
  info.height = image->info().height;
  info.sample_count = 1;
  info.usage = vk::ImageUsageFlagBits::eInputAttachment | vk::ImageUsageFlagBits::eColorAttachment;
  info.color_space = image->info().color_space;
  info.memory_flags = vk::MemoryPropertyFlagBits::eDeviceLocal;
  if (image->use_protected_memory()) {
    info.memory_flags |= vk::MemoryPropertyFlagBits::eProtected;
  }

  vk::Image vk_image =
      image_utils::CreateVkImage(escher_->vk_device(), info, vk::ImageLayout::eUndefined);

  auto allocator = escher_->gpu_allocator();
  auto mem_requirements = escher_->vk_device().getImageMemoryRequirements(vk_image);
  auto memory = allocator->AllocateMemory(mem_requirements, info.memory_flags);
  auto result = impl::NaiveImage::AdoptVkImage(escher_->resource_recycler(), info, vk_image, memory,
                                               vk::ImageLayout::eUndefined);

  transient_image_map_[image->info()] = result;
  return result;
}

vk::ImageCreateInfo RectangleCompositor::GetDefaultImageConstraints(const vk::Format& vk_format,
                                                                    vk::ImageUsageFlags usage) {
  vk::ImageCreateInfo create_info;
  create_info.imageType = vk::ImageType::e2D;
  create_info.extent = vk::Extent3D{1, 1, 1};
  create_info.flags = {};
  create_info.format = vk_format;
  create_info.mipLevels = 1;
  create_info.arrayLayers = 1;
  create_info.samples = vk::SampleCountFlagBits::e1;
  create_info.tiling = vk::ImageTiling::eOptimal;
  create_info.usage = usage;
  create_info.sharingMode = vk::SharingMode::eExclusive;
  create_info.initialLayout = vk::ImageLayout::eUndefined;
  return create_info;
}

}  // namespace escher
