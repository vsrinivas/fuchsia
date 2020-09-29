// Copyright 2018 The Fuchsia Authors. All rights reserved.// Use of this source code is governed by
// a BSD-style license that can be// found in the LICENSE file.
#include "src/ui/examples/escher/rainfall/rainfall_demo.h"

#include "src/ui/examples/escher/rainfall/scenes/flatland_demo_scene1.h"
#include "src/ui/examples/escher/rainfall/scenes/flatland_demo_scene2.h"
#include "src/ui/examples/escher/rainfall/scenes/scene.h"
#include "src/ui/lib/escher/defaults/default_shader_program_factory.h"
#include "src/ui/lib/escher/flatland/flatland_static_config.h"
#include "src/ui/lib/escher/mesh/tessellation.h"
#include "src/ui/lib/escher/renderer/batch_gpu_uploader.h"
#include "src/ui/lib/escher/renderer/render_funcs.h"
#include "src/ui/lib/escher/shape/mesh.h"
#include "src/ui/lib/escher/util/enum_utils.h"
#include "src/ui/lib/escher/util/trace_macros.h"
#include "src/ui/lib/escher/vk/shader_module_template.h"
#include "src/ui/lib/escher/vk/shader_program.h"
#include "src/ui/lib/escher/vk/texture.h"

using namespace escher;
namespace {

// Default 1x1 texture for Renderables with no texture.
TexturePtr CreateWhiteTexture(EscherWeakPtr escher, BatchGpuUploader* gpu_uploader) {
  FX_DCHECK(escher);
  uint8_t channels[4];
  channels[0] = channels[1] = channels[2] = channels[3] = 255;
  auto image = escher->NewRgbaImage(gpu_uploader, 1, 1, channels);
  return escher->NewTexture(std::move(image), vk::Filter::eNearest);
}

TexturePtr CreateDefaultTexture(EscherWeakPtr escher, CommandBuffer* cmd_buf,
                                std::shared_ptr<BatchGpuUploader> uploader,
                                SemaphorePtr upload_wait_semaphore) {
  auto result = CreateWhiteTexture(escher, uploader.get());
  cmd_buf->AddWaitSemaphore(
      std::move(upload_wait_semaphore),
      vk::PipelineStageFlagBits::eVertexInput | vk::PipelineStageFlagBits::eFragmentShader |
          vk::PipelineStageFlagBits::eColorAttachmentOutput | vk::PipelineStageFlagBits::eTransfer);
  return result;
}

TexturePtr CreateDepthBuffer(Escher* escher, const ImagePtr& output_image) {
  TexturePtr depth_buffer;
  RenderFuncs::ObtainDepthTexture(
      escher, output_image->use_protected_memory(), output_image->info(),
      escher->device()->caps().GetMatchingDepthStencilFormat().value, depth_buffer);
  return depth_buffer;
}

}  // anonymous namespace

RainfallDemo::RainfallDemo(escher::EscherWeakPtr escher_in, int argc, char** argv)
    : Demo(std::move(escher_in), "Rainfall Demo") {
  // Initialize filesystem with files before creating renderer; it will
  // use them to generate the necessary ShaderPrograms.
  escher()->shader_program_factory()->filesystem()->InitializeWithRealFiles(kFlatlandShaderPaths);
  renderer_ = std::make_unique<escher::RectangleCompositor>(escher());
}

RainfallDemo::~RainfallDemo() = default;

bool RainfallDemo::HandleKeyPress(std::string key) {
  char key_char = key[0];
  switch (key_char) {
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
    case '0':
      current_scene_ =
          static_cast<uint32_t>((demo_scenes_.size() + (key_char - '0') - 1) % demo_scenes_.size());
      FX_LOGS(INFO) << "Current scene index: " << current_scene_;
      return true;
    default:
      return Demo::HandleKeyPress(key);
  }
  return true;
}

void RainfallDemo::SetWindowSize(vk::Extent2D window_size) {
  if (window_size_ == window_size)
    return;
  window_size_ = window_size;
  InitializeDemoScenes();
}

void RainfallDemo::InitializeDemoScenes() {
  demo_scenes_.clear();
  demo_scenes_.emplace_back(new FlatlandDemoScene1(this));
  demo_scenes_.emplace_back(new FlatlandDemoScene2(this));
  for (auto& scene : demo_scenes_) {
    scene->Init();
  }
  stopwatch_.Start();
}

void RainfallDemo::DrawFrame(const escher::FramePtr& frame, const escher::ImagePtr& output_image,
                             const escher::SemaphorePtr& framebuffer_acquired) {
  TRACE_DURATION("gfx", "RainfallDemo::DrawFrame");
  FX_DCHECK(frame && output_image && renderer_);

  if (!default_texture_) {
    auto gpu_uploader =
        std::make_shared<escher::BatchGpuUploader>(escher()->GetWeakPtr(), frame->frame_number());
    auto upload_semaphore = escher::Semaphore::New(escher()->vk_device());
    gpu_uploader->AddSignalSemaphore(upload_semaphore);
    default_texture_ =
        CreateDefaultTexture(escher()->GetWeakPtr(), frame->cmds(), gpu_uploader, upload_semaphore);
    gpu_uploader->Submit();
  }
  if (!depth_buffer_) {
    depth_buffer_ = CreateDepthBuffer(escher(), output_image);
  }

  SetWindowSize({output_image->width(), output_image->height()});

  frame->cmds()->AddWaitSemaphore(framebuffer_acquired,
                                  vk::PipelineStageFlagBits::eColorAttachmentOutput);
  {
    TRACE_DURATION("gfx", "RainfallDemo::DrawFrame[scene]");
    FX_DCHECK(demo_scenes_[current_scene_]);
    demo_scenes_[current_scene_]->Update(stopwatch_);
    const auto& batch = demo_scenes_[current_scene_]->renderables();
    const auto& color_data = demo_scenes_[current_scene_]->color_data();
    std::vector<const TexturePtr> textures;
    for (uint32_t i = 0; i < batch.size(); i++) {
      textures.push_back(default_texture_);
    }
    renderer_->DrawBatch(frame->cmds(), batch, textures, color_data, output_image, depth_buffer_);
  }
}
