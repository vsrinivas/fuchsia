// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Implementation of a dummy User shell.
// This takes |recipe_url| as a command line argument and passes it to the
// Story Manager.

#include <mojo/system/main.h>

#include "apps/modular/story_manager/story_manager.mojom.h"
#include "apps/mozart/lib/skia/skia_surface_holder.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/views/interfaces/view_provider.mojom.h"
#include "apps/mozart/services/views/interfaces/view_token.mojom.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/synchronization/sleep.h"
#include "mojo/public/cpp/application/application_impl_base.h"
#include "mojo/public/cpp/application/connect.h"
#include "mojo/public/cpp/application/run_application.h"
#include "mojo/public/cpp/application/service_provider_impl.h"
#include "mojo/public/cpp/bindings/interface_ptr_set.h"
#include "mojo/public/cpp/bindings/strong_binding.h"
#include "mojo/public/cpp/bindings/strong_binding_set.h"
#include "mojo/public/interfaces/application/service_provider.mojom.h"
#include "third_party/skia/include/core/SkCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/core/SkRect.h"

namespace modular {

constexpr uint32_t kContentImageResourceId = 1;
constexpr char kExampleRecipeUrl[] = "mojo:example_recipe";
constexpr uint32_t kRootNodeId = mozart::kSceneRootNodeId;
constexpr float kSpeed = 0.25f;

using mojo::ApplicationImplBase;
using mojo::ConnectionContext;
using mojo::InterfaceHandle;
using mojo::InterfacePtr;
using mojo::InterfaceRequest;
using mojo::ServiceProviderImpl;
using mojo::Shell;
using mojo::StrongBindingSet;
using mojo::StrongBinding;
using mojo::StructPtr;

// TODO(alhaad): Copied from
// https://fuchsia.googlesource.com/mozart/+/master/examples/spinning_square/spinning_square.cc
// This is only a temporary way to test plumbing between device_runner and
// dummy_user_shell.
class SpinningSquareView : public mozart::BaseView {
 public:
  SpinningSquareView(
      mojo::InterfaceHandle<mojo::ApplicationConnector> app_connector,
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(app_connector.Pass(),
                 view_owner_request.Pass(),
                 "Spinning Square") {}
  ~SpinningSquareView() override {}

 private:
  // |BaseView|:
  void OnDraw() override {
    FTL_DCHECK(properties());
    auto update = mozart::SceneUpdate::New();
    const mojo::Size& size = *properties()->view_layout->size;
    if (size.width > 0 && size.height > 0) {
      mojo::RectF bounds;
      bounds.width = size.width;
      bounds.height = size.height;
      mozart::SkiaSurfaceHolder surface_holder(size);
      DrawContent(surface_holder.surface()->getCanvas(), size);
      auto content_resource = mozart::Resource::New();
      content_resource->set_image(mozart::ImageResource::New());
      content_resource->get_image()->image = surface_holder.TakeImage();
      update->resources.insert(kContentImageResourceId,
                               content_resource.Pass());
      auto root_node = mozart::Node::New();
      root_node->op = mozart::NodeOp::New();
      root_node->op->set_image(mozart::ImageNodeOp::New());
      root_node->op->get_image()->content_rect = bounds.Clone();
      root_node->op->get_image()->image_resource_id = kContentImageResourceId;
      update->nodes.insert(kRootNodeId, root_node.Pass());
    } else {
      auto root_node = mozart::Node::New();
      update->nodes.insert(kRootNodeId, root_node.Pass());
    }
    scene()->Update(update.Pass());
    scene()->Publish(CreateSceneMetadata());
    Invalidate();
  }
  void DrawContent(SkCanvas* canvas, const mojo::Size& size) {
    canvas->clear(SK_ColorBLUE);
    canvas->translate(size.width / 2, size.height / 2);
    float t =
        fmod(frame_tracker().frame_info().frame_time * 0.000001f * kSpeed, 1.f);
    canvas->rotate(360.f * t);
    SkPaint paint;
    paint.setColor(0xFFFF00FF);
    paint.setAntiAlias(true);
    float d = std::min(size.width, size.height) / 4;
    canvas->drawRect(SkRect::MakeLTRB(-d, -d, d, d), paint);
    canvas->flush();
  }
  FTL_DISALLOW_COPY_AND_ASSIGN(SpinningSquareView);
};

class DummyUserShellImpl : public UserShell {
 public:
  explicit DummyUserShellImpl(InterfaceRequest<UserShell> request)
      : binding_(this, std::move(request)) {}
  ~DummyUserShellImpl() override{};

  void SetStoryProvider(
      InterfaceHandle<StoryProvider> story_provider) override {
    story_provider_.Bind(story_provider.Pass());

    // Check for previous stories.
    story_provider_->PreviousStories([this](InterfaceHandle<Story> story) {
      FTL_DCHECK(!story.is_valid());
    });

    // Start a new story.
    story_provider_->StartNewStory(
        kExampleRecipeUrl, [this](InterfaceHandle<Story> story) {
          FTL_LOG(INFO) << "Received modular::Story from provider.";
          story_ptr_.Bind(story.Pass());
          story_ptr_->GetInfo([this](StructPtr<StoryInfo> story_info) {
            FTL_LOG(INFO) << "modular::Story received with url: "
                          << story_info->url
                          << " is_running: " << story_info->is_running;

            // Let the story run for 2500 milli-seconds before stopping.
            ftl::SleepFor(ftl::TimeDelta::FromMilliseconds(2500));

            story_ptr_->Stop();

            // Resume the stopped story.
            story_ptr_->Resume();
          });
        });
  }

 private:
  StrongBinding<UserShell> binding_;
  InterfacePtr<StoryProvider> story_provider_;
  InterfacePtr<Story> story_ptr_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DummyUserShellImpl);
};

class ViewProviderImpl : public mozart::ViewProvider {
 public:
  ViewProviderImpl(Shell* shell) : shell_(shell) {}
  ~ViewProviderImpl() override {}

 private:
  // |ViewProvider| override.
  void CreateView(
      mojo::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      mojo::InterfaceRequest<mojo::ServiceProvider> services) override {
    new SpinningSquareView(mojo::CreateApplicationConnector(shell_),
                           view_owner_request.Pass());
  }

  Shell* shell_;

  FTL_DISALLOW_COPY_AND_ASSIGN(ViewProviderImpl);
};

class DummyUserShellApp : public ApplicationImplBase {
 public:
  DummyUserShellApp() {}
  ~DummyUserShellApp() override {}

 private:
  bool OnAcceptConnection(ServiceProviderImpl* service_provider_impl) override {
    service_provider_impl->AddService<UserShell>(
        [this](const ConnectionContext& connection_context,
               InterfaceRequest<UserShell> user_shell_request) {
          new DummyUserShellImpl(std::move(user_shell_request));
        });
    service_provider_impl->AddService<mozart::ViewProvider>(
        [this](const ConnectionContext& connection_context,
               InterfaceRequest<mozart::ViewProvider> view_provider_request) {
          bindings_.AddBinding(new ViewProviderImpl(shell()),
                               view_provider_request.Pass());
        });
    return true;
  }

  StrongBindingSet<mozart::ViewProvider> bindings_;
};

}  // namespace modular

MojoResult MojoMain(MojoHandle application_request) {
  FTL_LOG(INFO) << "dummy_user_shell main";
  modular::DummyUserShellApp app;
  return mojo::RunApplication(application_request, &app);
}
