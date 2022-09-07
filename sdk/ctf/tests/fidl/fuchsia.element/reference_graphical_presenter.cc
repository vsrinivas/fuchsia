// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/element/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/sys/cpp/component_context.h>
#include <stdlib.h>

#include <utility>

class ViewControllerImpl : public fuchsia::element::ViewController {
 public:
  void set_on_dismiss(std::function<void(ViewControllerImpl*)> on_dismiss) {
    on_dismiss_ = std::move(on_dismiss);
  }

 private:
  // |fuchsia.element.ViewController|
  void Dismiss() override { on_dismiss_(this); }

  std::function<void(ViewControllerImpl*)> on_dismiss_ = [](auto view_controller) {};
};

class GraphicalPresenterImpl : fuchsia::element::GraphicalPresenter {
 public:
  fidl::InterfaceRequestHandler<fuchsia::element::GraphicalPresenter> GetHandler() {
    return bindings_.GetHandler(this);
  }

 private:
  // |fuchsia.element.GraphicalPresenter|
  void PresentView(
      fuchsia::element::ViewSpec view_spec,
      fidl::InterfaceHandle<fuchsia::element::AnnotationController> annotation_controller,
      fidl::InterfaceRequest<fuchsia::element::ViewController> view_controller_request,
      PresentViewCallback callback) override {
    auto view_controller = std::make_unique<ViewControllerImpl>();
    view_controller->set_on_dismiss([this](ViewControllerImpl* view_controller) {
      view_controller_bindings_.CloseBinding(view_controller, ZX_OK);
    });
    view_controller_bindings_.AddBinding(view_controller.get(), std::move(view_controller_request));
    view_controllers_.push_back(std::move(view_controller));

    fuchsia::element::GraphicalPresenter_PresentView_Result result;
    result.set_response({});
    callback(std::move(result));
  }

  fidl::BindingSet<fuchsia::element::GraphicalPresenter> bindings_;
  std::vector<std::unique_ptr<ViewControllerImpl>> view_controllers_;
  fidl::BindingSet<fuchsia::element::ViewController> view_controller_bindings_;
};

int main(int argc, const char** argv) {
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto context = sys::ComponentContext::CreateAndServeOutgoingDirectory();

  auto graphical_presenter = std::make_unique<GraphicalPresenterImpl>();
  context->outgoing()->AddPublicService(graphical_presenter->GetHandler());

  loop.Run();
  return EXIT_SUCCESS;
}
