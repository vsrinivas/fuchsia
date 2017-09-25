// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/module/fidl/module.fidl.h"
#include "lib/module/fidl/module_context.fidl.h"
#include "lib/ui/view_framework/base_view.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "peridot/examples/counter_cpp/store.h"
#include "peridot/lib/fidl/single_service_app.h"

namespace {

constexpr float kBackgroundElevation = 0.f;
constexpr float kSquareElevation = 8.f;
constexpr int kTickRotationDegrees = 45;
constexpr int kAnimationDelayInMs = 50;

constexpr char kModuleName[] = "Module2Impl";

class Module2View : public mozart::BaseView {
 public:
  explicit Module2View(
      modular_example::Store* const store,
      mozart::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(view_manager),
                 std::move(view_owner_request),
                 "Module2Impl"),
        store_(store),
        background_node_(session()),
        square_node_(session()) {
    scenic_lib::Material background_material(session());
    background_material.SetColor(0x67, 0x3a, 0xb7, 0xff);  // Deep Purple 500
    background_node_.SetMaterial(background_material);
    parent_node().AddChild(background_node_);

    scenic_lib::Material square_material(session());
    square_material.SetColor(0x29, 0x79, 0xff, 0xff);  // Blue A400
    square_node_.SetMaterial(square_material);
    parent_node().AddChild(square_node_);
  }

  ~Module2View() override = default;

 private:
  // Copied from
  // https://fuchsia.googlesource.com/mozart/+/master/examples/spinning_square/spinning_square.cc
  // |BaseView|:
  void OnSceneInvalidated(
      scenic::PresentationInfoPtr /*presentation_info*/) override {
    if (!has_logical_size()) {
      return;
    }

    const float center_x = logical_size().width * .5f;
    const float center_y = logical_size().height * .5f;
    const float square_size =
        std::min(logical_size().width, logical_size().height) * .6f;
    const float angle =
        kTickRotationDegrees * store_->counter.counter * M_PI * 2;

    scenic_lib::Rectangle background_shape(session(), logical_size().width,
                                           logical_size().height);
    background_node_.SetShape(background_shape);
    background_node_.SetTranslation(
        (float[]){center_x, center_y, kBackgroundElevation});

    scenic_lib::Rectangle square_shape(session(), square_size, square_size);
    square_node_.SetShape(square_shape);
    square_node_.SetTranslation(
        (float[]){center_x, center_y, kSquareElevation});
    square_node_.SetRotation(
        (float[]){0.f, 0.f, sinf(angle * .5f), cosf(angle * .5f)});
  }

  modular_example::Store* const store_;
  scenic_lib::ShapeNode background_node_;
  scenic_lib::ShapeNode square_node_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Module2View);
};

// Module implementation that acts as a leaf module. It implements Module.
class Module2App : public modular::SingleServiceApp<modular::Module> {
 public:
  explicit Module2App() : store_(kModuleName), weak_ptr_factory_(this) {
    store_.AddCallback([this] {
      if (view_) {
        view_->InvalidateScene();
      }
    });
    store_.AddCallback([this] { IncrementCounterAction(); });
  }

  ~Module2App() override = default;

 private:
  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<app::ServiceProvider> /*services*/) override {
    view_ = std::make_unique<Module2View>(
        &store_,
        application_context()
            ->ConnectToEnvironmentService<mozart::ViewManager>(),
        std::move(view_owner_request));
  }

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> /*incoming_services*/,
      fidl::InterfaceRequest<app::ServiceProvider> /*outgoing_services*/)
      override {
    module_context_.Bind(std::move(module_context));
    modular::LinkPtr link;
    module_context_->GetLink(nullptr, link.NewRequest());
    store_.Initialize(std::move(link));
  }

  // |Lifecycle|
  void Terminate() override {
    store_.Stop();
    fsl::MessageLoop::GetCurrent()->QuitNow();
  }

  void IncrementCounterAction() {
    if (store_.counter.sender == kModuleName || store_.counter.counter > 11) {
      return;
    }

    fxl::WeakPtr<Module2App> module_ptr = weak_ptr_factory_.GetWeakPtr();
    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this, module_ptr] {
          if (!module_ptr.get() || store_.terminating()) {
            return;
          }

          store_.counter.sender = kModuleName;
          store_.counter.counter += 1;

          FXL_LOG(INFO) << "Module2Impl COUNT " << store_.counter.counter;

          store_.MarkDirty();
          store_.ModelChanged();
        },
        fxl::TimeDelta::FromMilliseconds(kAnimationDelayInMs));
  }

  std::unique_ptr<Module2View> view_;
  fidl::InterfacePtr<modular::ModuleContext> module_context_;
  modular_example::Store store_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  fxl::WeakPtrFactory<Module2App> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Module2App);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  Module2App app;
  loop.Run();
  return 0;
}
