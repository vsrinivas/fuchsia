// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>

#include "lib/app/cpp/connect.h"
#include "apps/modular/examples/counter_cpp/calculator.fidl.h"
#include "apps/modular/examples/counter_cpp/store.h"
#include "apps/modular/lib/fidl/single_service_app.h"
#include "apps/modular/services/module/module.fidl.h"
#include "apps/modular/services/module/module_context.fidl.h"
#include "lib/ui/view_framework/base_view.h"
#include "lib/ui/views/fidl/view_manager.fidl.h"
#include "lib/fidl/cpp/bindings/interface_request.h"
#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/memory/weak_ptr.h"
#include "lib/fxl/time/time_delta.h"
#include "lib/fsl/tasks/message_loop.h"

namespace {

constexpr float kBackgroundElevation = 0.f;
constexpr float kSquareElevation = 8.f;
constexpr int kTickRotationDegrees = 45;
constexpr int kAnimationDelayInMs = 50;

constexpr char kModuleName[] = "Module1Impl";

class Module1View : public mozart::BaseView {
 public:
  explicit Module1View(
      modular_example::Store* const store,
      mozart::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request)
      : BaseView(std::move(view_manager),
                 std::move(view_owner_request),
                 kModuleName),
        store_(store),
        background_node_(session()),
        square_node_(session()) {
    scenic_lib::Material background_material(session());
    background_material.SetColor(0x67, 0x3a, 0xb7, 0xff);  // Deep Purple 500
    background_node_.SetMaterial(background_material);
    parent_node().AddChild(background_node_);

    scenic_lib::Material square_material(session());
    square_material.SetColor(0x00, 0xe6, 0x76, 0xff);  // Green A400
    square_node_.SetMaterial(square_material);
    parent_node().AddChild(square_node_);
  }

  ~Module1View() override = default;

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

  FXL_DISALLOW_COPY_AND_ASSIGN(Module1View);
};

class MultiplierImpl : public modular::examples::Multiplier {
 public:
  MultiplierImpl() = default;

 private:
  // |Multiplier|
  void Multiply(int32_t a, int32_t b, const MultiplyCallback& result) override {
    result(a * b);
  }

  FXL_DISALLOW_COPY_AND_ASSIGN(MultiplierImpl);
};

// Module implementation that acts as a leaf module. It implements Module.
class Module1App : modular::SingleServiceApp<modular::Module> {
 public:
  explicit Module1App() : store_(kModuleName), weak_ptr_factory_(this) {
    // TODO(mesch): The callbacks seem to have a sequential relationship.
    // It seems to me there should be a single callback that does all three
    // things in a sequence. Since the result InvalidateScene() happens only
    // (asynchonously) later, the order here really doesn't matter, but it's
    // only accidentally so.
    store_.AddCallback([this] {
      if (view_) {
        view_->InvalidateScene();
      }
    });
    store_.AddCallback([this] { IncrementCounterAction(); });
    store_.AddCallback([this] { CheckForDone(); });
  }

  ~Module1App() override = default;

 private:
  // |SingleServiceApp|
  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
      fidl::InterfaceRequest<app::ServiceProvider> /*services*/) override {
    view_ = std::make_unique<Module1View>(
        &store_,
        application_context()
            ->ConnectToEnvironmentService<mozart::ViewManager>(),
        std::move(view_owner_request));
  }

  // |Module|
  void Initialize(
      fidl::InterfaceHandle<modular::ModuleContext> module_context,
      fidl::InterfaceHandle<app::ServiceProvider> incoming_services,
      fidl::InterfaceRequest<app::ServiceProvider> outgoing_services) override {
    FXL_CHECK(incoming_services.is_valid());
    FXL_CHECK(outgoing_services.is_pending());

    module_context_.Bind(std::move(module_context));
    modular::LinkPtr link;
    module_context_->GetLink(nullptr, link.NewRequest());
    store_.Initialize(std::move(link));

    // Provide services to the recipe module.
    outgoing_services_.AddBinding(std::move(outgoing_services));
    outgoing_services_.AddService<modular::examples::Multiplier>(
        [this](fidl::InterfaceRequest<modular::examples::Multiplier> req) {
          multiplier_clients_.AddBinding(&multiplier_service_, std::move(req));
        });

    // This exercises the incoming services we get from the recipe.
    FXL_CHECK(incoming_services.is_valid());
    auto recipe_services =
        app::ServiceProviderPtr::Create(std::move(incoming_services));

    auto adder_service =
        app::ConnectToService<modular::examples::Adder>(recipe_services.get());
    adder_service.set_connection_error_handler([] {
      FXL_CHECK(false) << "Uh oh, Connection to Adder closed by the recipe.";
    });
    adder_service->Add(4, 4,
                       fxl::MakeCopyable([adder_service = std::move(
                                              adder_service)](int32_t result) {
                         FXL_CHECK(result == 8);
                         FXL_LOG(INFO) << "Incoming Adder service: 4 + 4 is 8.";
                       }));
  }

  // |Lifecycle|
  void Terminate() override {
    store_.Stop();
    fsl::MessageLoop::GetCurrent()->QuitNow();
  }

  void CheckForDone() {
    if (store_.counter.counter > 10) {
      module_context_->Done();
    }
  }

  void IncrementCounterAction() {
    if (store_.counter.sender == kModuleName || store_.counter.counter > 10) {
      return;
    }

    fxl::WeakPtr<Module1App> module_ptr = weak_ptr_factory_.GetWeakPtr();
    fsl::MessageLoop::GetCurrent()->task_runner()->PostDelayedTask(
        [this, module_ptr] {
          if (!module_ptr.get() || store_.terminating()) {
            return;
          }

          store_.counter.sender = kModuleName;
          store_.counter.counter += 1;

          FXL_LOG(INFO) << "Module1Impl COUNT " << store_.counter.counter;

          store_.MarkDirty();
          store_.ModelChanged();
        },
        fxl::TimeDelta::FromMilliseconds(kAnimationDelayInMs));
  }

  // This is a ServiceProvider we expose to our parent (recipe) module, to
  // demonstrate the use of a service exchange.
  fidl::BindingSet<modular::examples::Multiplier> multiplier_clients_;
  MultiplierImpl multiplier_service_;
  app::ServiceNamespace outgoing_services_;

  std::unique_ptr<Module1View> view_;
  modular::ModuleContextPtr module_context_;
  modular_example::Store store_;

  // Note: This should remain the last member so it'll be destroyed and
  // invalidate its weak pointers before any other members are destroyed.
  fxl::WeakPtrFactory<Module1App> weak_ptr_factory_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Module1App);
};

}  // namespace

int main(int /*argc*/, const char** /*argv*/) {
  fsl::MessageLoop loop;
  Module1App app;
  loop.Run();
  return 0;
}
