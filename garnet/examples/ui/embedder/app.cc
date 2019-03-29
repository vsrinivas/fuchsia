// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/examples/ui/embedder/app.h"

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/ui/views/cpp/fidl.h>
#include <src/lib/fxl/logging.h>
#include <lib/svc/cpp/services.h>
#include <lib/sys/cpp/file_descriptor.h>
#include <lib/ui/scenic/cpp/commands.h>
#include <lib/ui/scenic/cpp/view_token_pair.h>
#include <lib/zx/time.h>
#include <zircon/types.h>

#include <unistd.h>

#include "example_view_provider_service.h"

// Returns a human-readable string for a given embedder process type -
// either container or subview.
static const char* AppTypeString(embedder::AppType type) {
  if (type == embedder::AppType::CONTAINER) {
    return "[CONTAINER] ";
  } else if (type == embedder::AppType::SUBVIEW) {
    return "[SUBVIEW] ";
  } else {
    FXL_DCHECK(false);
  }
  return nullptr;
}

namespace embedder {

static fuchsia::sys::ComponentControllerPtr s_subview_controller;

App::App(async::Loop* loop, AppType type)
    : startup_context_(component::StartupContext::CreateFromStartupInfo()),
      loop_(loop),
      type_(type) {
  // Connect the ExampleViewProviderService.
  if (type_ == AppType::CONTAINER) {
    // Launch the subview app.  Clone our stdout and stderr file descriptors
    // into it so output (FXL_LOG, etc) from the subview app will show up as
    // if it came from us.
    component::Services subview_services;
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.out = sys::CloneFileDescriptor(STDOUT_FILENO);
    launch_info.err = sys::CloneFileDescriptor(STDERR_FILENO);
    launch_info.url = "fuchsia-pkg://fuchsia.com/embedder#meta/subview.cmx";
    launch_info.directory_request = subview_services.NewRequest();
    startup_context_->launcher()->CreateComponent(
        std::move(launch_info), s_subview_controller.NewRequest());

    subview_services.ConnectToService(view_provider_.NewRequest(),
                                      "view_provider");
  } else if (type_ == AppType::SUBVIEW) {
    view_provider_impl_ = std::make_unique<ExampleViewProviderService>(
        startup_context_.get(), [this](ViewContext context) {
          // Bind the ServiceProviders, ourselves as the ourgoing one.
          incoming_services_.Bind(std::move(context.incoming_services));
          service_bindings_.AddBinding(this,
                                       std::move(context.outgoing_services));

          // Create the View resource.
          view_id_ = session_->AllocResourceId();
          session_->Enqueue(scenic::NewCreateViewCmd(
              view_id_, std::move(context.token), "Subview"));

          if (root_node_id_ != 0) {
            session_->Enqueue(scenic::NewAddChildCmd(view_id_, root_node_id_));
          }
        });
  }

  // Connect to the global Scenic service and begin a session.
  FXL_LOG(INFO) << AppTypeString(type_) << "Connecting to Scenic service.";
  scenic_ = startup_context_
                ->ConnectToEnvironmentService<fuchsia::ui::scenic::Scenic>();
  scenic_.set_error_handler([this](zx_status_t status) {
    FXL_LOG(INFO) << AppTypeString(type_)
                  << "Scenic error.  Connection dropped.";
    ReleaseSessionResources();
    loop_->Quit();
  });
  FXL_LOG(INFO) << AppTypeString(type_) << "Creating new session.";
  session_ = std::make_unique<scenic::Session>(scenic_.get());
  session_->SetDebugName("Embedder");
  session_->set_error_handler([this](zx_status_t status) {
    FXL_LOG(INFO) << AppTypeString(type_)
                  << "Session error.  Connection dropped.";
    ReleaseSessionResources();
    loop_->Quit();
  });

  if (type_ == AppType::CONTAINER) {
    auto [view_token, view_holder_token] = scenic::NewViewTokenPair();

    // Create the subview and bind the ServiceProviders.
    FXL_LOG(INFO) << AppTypeString(type_) << "Creating view.";
    fuchsia::sys::ServiceProviderPtr outgoing_services;
    outgoing_services.Bind(service_bindings_.AddBinding(this));
    view_provider_->CreateView(std::move(view_token.value),
                               incoming_services_.NewRequest(),
                               std::move(outgoing_services));

    // Create the ViewHolder resource that will proxy the view.
    view_id_ = session_->AllocResourceId();
    session_->Enqueue(scenic::NewCreateViewHolderCmd(
        view_id_, std::move(view_holder_token), "Subview-Holder"));
  }

  // Close the session and quit after several seconds.
  async::PostDelayedTask(
      loop_->dispatcher(),
      [this] {
        FXL_LOG(INFO) << AppTypeString(type_) << "Closing session.";
        ReleaseSessionResources();
        loop_->Quit();
      },
      zx::sec(30));

  // Set up a scene after we get display info, since the scene relies on the
  // size of the display.
  scenic_->GetDisplayInfo([this](fuchsia::ui::gfx::DisplayInfo display_info) {
    const float display_width = static_cast<float>(display_info.width_in_px);
    const float display_height = static_cast<float>(display_info.height_in_px);
    CreateScene(display_width, display_height);

    Update(zx_clock_get(ZX_CLOCK_MONOTONIC));
  });
}

App::~App() { ReleaseSessionResources(); }

void App::ReleaseSessionResources() {
  if (session_ != nullptr) {
    if (view_id_ != 0) {
      session_->ReleaseResource(view_id_);
    }
    compositor_.reset();
    camera_.reset();
    session_->Flush();
    session_.reset();
  }
}

void App::Update(uint64_t next_presentation_time) {
  session_->Present(
      next_presentation_time, [this](fuchsia::images::PresentationInfo info) {
        Update(info.presentation_time + info.presentation_interval);
      });
}

void App::CreateScene(float display_width, float display_height) {
  // The finished scene should contain 2 rounded rectangles, each centered in
  // the screen.  The container process is represented by the larger green
  // rectangle, which the subview process is represented by the smaller pink
  // rectangle.
  auto session_ptr = session_.get();
  scenic::EntityNode root_node(session_ptr);
  root_node_id_ = root_node.id();

  if (type_ == AppType::CONTAINER) {
    compositor_ = std::make_unique<scenic::DisplayCompositor>(session_ptr);
    scenic::LayerStack layer_stack(session_ptr);
    scenic::Layer layer(session_ptr);
    scenic::Renderer renderer(session_ptr);
    scenic::Scene scene(session_ptr);
    camera_ = std::make_unique<scenic::Camera>(scene);

    compositor_->SetLayerStack(layer_stack);
    layer_stack.AddLayer(layer);
    layer.SetSize(display_width, display_height);
    layer.SetRenderer(renderer);
    renderer.SetCamera(camera_->id());

    // Set up lights.
    scenic::AmbientLight ambient_light(session_ptr);
    scenic::DirectionalLight directional_light(session_ptr);
    scene.AddLight(ambient_light);
    scene.AddLight(directional_light);
    ambient_light.SetColor(0.3f, 0.3f, 0.3f);
    directional_light.SetColor(0.7f, 0.7f, 0.7f);
    directional_light.SetDirection(1.f, 1.f, -2.f);

    scene.AddChild(root_node_id_);
  }

  static const float kBackgroundMargin =
      (type_ == AppType::CONTAINER) ? 100.f : 250.f;
  static const float background_width = display_width - 2.f * kBackgroundMargin;
  static const float background_height =
      display_height - 2.f * kBackgroundMargin;
  scenic::ShapeNode background_node(session_ptr);
  scenic::RoundedRectangle background_shape(
      session_ptr, background_width, background_height, 20.f, 20.f, 80.f, 10.f);
  scenic::Material background_material(session_ptr);
  if (type_ == AppType::CONTAINER) {
    background_material.SetColor(120, 255, 120, 255);
  } else if (type_ == AppType::SUBVIEW) {
    background_material.SetColor(218, 112, 214, 255);
  }
  background_node.SetShape(background_shape);
  background_node.SetMaterial(background_material);
  root_node.SetClip(0, true);
  if (type_ == AppType::CONTAINER) {
    root_node.SetTranslation(kBackgroundMargin + background_width * 0.5f,
                             kBackgroundMargin + background_height * 0.5f,
                             -1.f);
  } else {
    root_node.SetTranslation(0.f, 0.f, -1.f);
  }
  root_node.AddPart(background_node);

  if (view_id_ != 0) {
    if (type_ == AppType::CONTAINER) {
      session_->Enqueue(scenic::NewAddChildCmd(root_node_id_, view_id_));
    } else if (type_ == AppType::SUBVIEW) {
      session_->Enqueue(scenic::NewAddChildCmd(view_id_, root_node_id_));
    }
  }
}

}  // namespace embedder
