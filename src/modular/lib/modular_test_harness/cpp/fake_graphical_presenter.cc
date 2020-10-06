// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/lib/modular_test_harness/cpp/fake_graphical_presenter.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

namespace modular_testing {

void FakeViewController::Annotate(fuchsia::session::Annotations annotations,
                                  AnnotateCallback callback) {
  if (fake_graphical_presenter_->on_annotate_) {
    fake_graphical_presenter_->on_annotate_(std::move(annotations));
  }

  if (callback) {
    callback();
  }
}

void FakeViewController::Dismiss() {
  if (fake_graphical_presenter_->on_dismiss_) {
    fake_graphical_presenter_->on_dismiss_();
  }
}

FakeGraphicalPresenter::FakeGraphicalPresenter(FakeComponent::Args args)
    : FakeComponent(std::move(args)) {}

FakeGraphicalPresenter::~FakeGraphicalPresenter() = default;

// static
std::unique_ptr<FakeGraphicalPresenter> FakeGraphicalPresenter::CreateWithDefaultOptions() {
  return std::make_unique<FakeGraphicalPresenter>(modular_testing::FakeComponent::Args{
      .url = modular_testing::TestHarnessBuilder::GenerateFakeUrl("FakeGraphicalPresenter"),
      .sandbox_services = FakeGraphicalPresenter::GetDefaultSandboxServices()});
}

// static
std::vector<std::string> FakeGraphicalPresenter::GetDefaultSandboxServices() {
  return {fuchsia::modular::ComponentContext::Name_, fuchsia::modular::SessionShellContext::Name_,
          fuchsia::modular::PuppetMaster::Name_};
}

void FakeGraphicalPresenter::CloseFirstViewController() {
  view_controller_bindings_.CloseBinding(view_controllers_[0], ZX_OK);
  view_controllers_.erase(view_controllers_.begin());
}

void FakeGraphicalPresenter::OnCreate(fuchsia::sys::StartupInfo startup_info) {
  component_context()->svc()->Connect(session_shell_context_.NewRequest());
  session_shell_context_->GetStoryProvider(story_provider_.NewRequest());

  fit::function<void(fidl::InterfaceRequest<fuchsia::session::GraphicalPresenter> request)>
      graphical_presenter_handler =
          [this](fidl::InterfaceRequest<fuchsia::session::GraphicalPresenter> request) {
            if (on_graphical_presenter_connected_) {
              on_graphical_presenter_connected_();
            }
            graphical_presenter_bindings_.AddBinding(this, std::move(request),
                                                     /* dispatcher =*/nullptr,
                                                     std::move(on_graphical_presenter_error_));
          };

  component_context()->outgoing()->AddPublicService(std::move(graphical_presenter_handler));
}

void FakeGraphicalPresenter::OnDestroy() {
  if (on_destroy_) {
    on_destroy_();
  }
}

void FakeGraphicalPresenter::PresentView(
    fuchsia::session::ViewSpec view_spec,
    ::fidl::InterfaceRequest<fuchsia::session::ViewController> view_controller_request) {
  auto view_controller = std::make_shared<FakeViewController>(this);
  view_controller_bindings_.AddBinding(view_controller.get(), std::move(view_controller_request));
  view_controllers_.push_back(std::move(view_controller));

  if (on_present_view_) {
    on_present_view_(std::move(view_spec));
  }
}

}  // namespace modular_testing
