// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cmath>
#include <memory>
#include <sstream>

#include "application/lib/app/application_context.h"
#include "apps/mozart/services/scene/scene_manager.fidl.h"
#include "apps/mozart/src/scene/renderer/renderer.h"
#include "apps/mozart/src/scene/resources/dump_visitor.h"
#include "apps/mozart/src/scene/scene_manager_impl.h"
#include "escher/escher.h"
#include "escher/escher_process_init.h"
#include "escher/geometry/types.h"
#include "escher/material/material.h"
#include "escher/renderer/paper_renderer.h"
#include "escher/scene/model.h"
#include "escher/scene/stage.h"
#include "escher/vk/vulkan_swapchain_helper.h"
#include "lib/escher/examples/common/demo.h"
#include "lib/escher/examples/common/demo_harness_fuchsia.h"
#include "lib/fidl/cpp/bindings/binding_set.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/log_settings.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/mtl/tasks/message_loop.h"

static constexpr uint32_t kScreenWidth = 2160;
static constexpr uint32_t kScreenHeight = 1440;

// Material design places objects from 0.0f to 24.0f.
static constexpr float kNear = 24.f;
static constexpr float kFar = 0.f;

namespace mozart {
namespace scene {

class SceneManagerImpl;

class HelloSceneManagerService : public Demo {
 public:
  explicit HelloSceneManagerService(DemoHarnessFuchsia* harness)
      : Demo(harness),
        application_context_(harness->application_context()),
        renderer_(escher()->NewPaperRenderer()),
        swapchain_helper_(harness->GetVulkanSwapchain(), renderer_),
        scene_manager_(std::make_unique<SceneManagerImpl>(escher())),
        binding_(scene_manager_.get()) {
    FTL_DCHECK(application_context_);

    AddOutgoingServices();
    InitializeEscherStage();
  }

  ~HelloSceneManagerService() {
    FTL_LOG(INFO) << "HelloSceneManagerService: shutting down";
  }

  void DrawFrame() override {
    scene_manager_->BeginFrame();
    Renderer* renderer = scene_manager_->renderer();

    // TODO: propagate dirty flags up the tree, and only render if dirty

    // For now, just assume the first Scene created is the root of the tree.
    const auto& scenes = scene_manager_->session_context().scenes();
    if (scenes.size() > 0) {
      if (FTL_VLOG_IS_ON(3)) {
        std::ostringstream output;
        DumpVisitor visitor(output);
        scenes[0]->Accept(&visitor);
        FTL_VLOG(3) << "Scene graph contents\n" << output.str();
      }

      escher::Model model(renderer->CreateDisplayList(
          scenes[0].get(), escher::vec2(kScreenWidth, kScreenHeight)));
      swapchain_helper_.DrawFrame(stage_, model);
    }
  }

 private:
  void AddOutgoingServices() {
    application_context_->outgoing_services()
        ->AddService<mozart2::SceneManager>([this](fidl::InterfaceRequest<
                                                   mozart2::SceneManager>
                                                       request) {
          if (binding_.is_bound()) {
            FTL_LOG(WARNING) << "HelloSceneManagerService: SceneManager "
                                "already bound to client";
          } else {
            FTL_LOG(INFO)
                << "HelloSceneManagerService: binding client to SceneManager";
            binding_.Bind(std::move(request));
            binding_.set_connection_error_handler([this] {
              FTL_LOG(INFO) << "HelloSceneManagerService: connection to client "
                               "was closed";
              scene_manager_.reset();
              mtl::MessageLoop::GetCurrent()->QuitNow();
            });
          }
        });
  }

  void InitializeEscherStage() {
    stage_.Resize(escher::SizeI(kScreenWidth, kScreenHeight), 1.0,
                  escher::SizeI(0, 0));
    stage_.set_viewing_volume(
        escher::ViewingVolume(kScreenWidth, kScreenHeight, kNear, kFar));
    stage_.set_key_light(escher::DirectionalLight(
        escher::vec2(1.5f * M_PI, 1.5f * M_PI), 0.15f * M_PI, 0.7f));
    stage_.set_fill_light(escher::AmbientLight(0.3f));
  }

  app::ApplicationContext* const application_context_;
  escher::PaperRendererPtr renderer_;
  escher::VulkanSwapchainHelper swapchain_helper_;
  escher::Stage stage_;
  std::unique_ptr<SceneManagerImpl> scene_manager_;
  fidl::Binding<mozart2::SceneManager> binding_;

  FTL_DISALLOW_COPY_AND_ASSIGN(HelloSceneManagerService);
};

}  // namespace scene
}  // namespace mozart

int main(int argc, const char** argv) {
  FTL_LOG(INFO) << "HelloSceneManagerService: entering main()";

  auto command_line = ftl::CommandLineFromArgcArgv(argc, argv);
  if (!ftl::SetLogSettingsFromCommandLine(command_line))
    return 1;

  auto harness = DemoHarness::New(
      DemoHarness::WindowParams{"Mozart SceneManager Example", kScreenWidth,
                                kScreenHeight, 2, false},
      DemoHarness::InstanceParams());

  {
    mozart::scene::HelloSceneManagerService scene_manager_app(
        static_cast<DemoHarnessFuchsia*>(harness.get()));
    harness->Run(&scene_manager_app);
  }
  harness->Shutdown();
  return 0;
}
