// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_GRAPHICAL_PRESENTER_H_
#define SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_GRAPHICAL_PRESENTER_H_

#include <fuchsia/element/cpp/fidl.h>
#include <lib/modular/testing/cpp/fake_component.h>

namespace modular_testing {

class FakeGraphicalPresenter;

class FakeViewController : public fuchsia::element::ViewController {
 public:
  explicit FakeViewController(FakeGraphicalPresenter* fake_graphical_presenter)
      : fake_graphical_presenter_(fake_graphical_presenter) {}

  // |ViewController| (composed |AnnotationController|)
  void UpdateAnnotations(std::vector<fuchsia::element::Annotation> annotations_to_set,
                         std::vector<fuchsia::element::AnnotationKey> annotations_to_delete,
                         UpdateAnnotationsCallback callback) override;

  // |ViewController| (composed |AnnotationController|)
  void GetAnnotations(GetAnnotationsCallback callback) override;

  // |ViewController|
  void Dismiss() override;

 private:
  FakeGraphicalPresenter* fake_graphical_presenter_;
};

// Fake version of a session shell that exports GraphicalPresenter instead of the SessionShell
// service.
//
// EXAMPLE USAGE:
//
// ...
// modular_testing::TestHarnessBuilder builder;
// auto fake_graphical_presenter = FakeGraphicalPresenter::CreateWithDefaultOptions();
//
// builder.InterceptSessionShell(fake_graphical_presenter.BuildInterceptOptions());
// builder.BuildAndRun(test_harness());
//
// // Wait for the session shell to be intercepted.
// RunLoopUntil([&] { return fake_graphical_presenter->is_running(); });
// ...
class FakeGraphicalPresenter : public modular_testing::FakeComponent,
                               fuchsia::element::GraphicalPresenter {
 public:
  using StoryShellRequest = fidl::InterfaceRequest<fuchsia::modular::StoryShell>;

  explicit FakeGraphicalPresenter(FakeComponent::Args args);
  ~FakeGraphicalPresenter() override;

  // Instantiates a FakeGraphicalPresenter with a randomly generated URL and default sandbox
  // services (see GetDefaultSandboxServices()).
  static std::unique_ptr<FakeGraphicalPresenter> CreateWithDefaultOptions();

  // Returns the default list of services (capabilities) a session shell expects in its namespace.
  // This method is useful when setting up a session shell for interception.
  //
  // Default services:
  //  * fuchsia.modular.ComponentContext
  //  * fuchsia.modular.SessionShellContext
  //  * fuchsia.modular.PuppetMaster
  static std::vector<std::string> GetDefaultSandboxServices();

  // Requires: FakeComponent::is_running()
  fuchsia::modular::StoryProvider* story_provider() { return story_provider_.get(); }

  // Requires: FakeComponent::is_running()
  fuchsia::modular::SessionShellContext* session_shell_context() {
    return session_shell_context_.get();
  }

  void CloseFirstViewController();
  void CloseAllViewControllers() { view_controller_bindings_.CloseAll(); }

  void set_on_destroy(fit::function<void()> on_destroy) { on_destroy_ = std::move(on_destroy); }

  void set_on_graphical_presenter_connected(
      fit::function<void()> on_graphical_presenter_connected) {
    on_graphical_presenter_connected_ = std::move(on_graphical_presenter_connected);
  }

  void set_on_graphical_presenter_error(
      fit::function<void(zx_status_t)> on_graphical_presenter_error) {
    on_graphical_presenter_error_ = std::move(on_graphical_presenter_error);
  }

  void set_on_present_view(
      fit::function<void(fuchsia::element::ViewSpec view_spec)> on_present_view) {
    on_present_view_ = std::move(on_present_view);
  }

  void set_on_update_annotations(
      fit::function<void(std::vector<fuchsia::element::Annotation> annotations_to_set,
                         std::vector<fuchsia::element::AnnotationKey> annotations_to_delete)>
          on_update_annotations) {
    on_update_annotations_ = std::move(on_update_annotations);
  }

  void set_on_dismiss(fit::function<void()> on_dismiss) { on_dismiss_ = std::move(on_dismiss); }

 private:
  // |modular_testing::FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override;

  // |modular_testing::FakeComponent|
  void OnDestroy() override;

  // |fuchsia::element::GraphicalPresenter|
  void PresentView(
      fuchsia::element::ViewSpec view_spec,
      ::fidl::InterfaceRequest<fuchsia::element::ViewController> view_controller_request,
      PresentViewCallback callback) override;

  fuchsia::modular::SessionShellContextPtr session_shell_context_;
  fuchsia::modular::StoryProviderPtr story_provider_;

  fidl::BindingSet<fuchsia::element::GraphicalPresenter> graphical_presenter_bindings_;
  fidl::BindingSet<fuchsia::element::ViewController> view_controller_bindings_;

  std::vector<std::shared_ptr<FakeViewController>> view_controllers_;

  fit::function<void()> on_destroy_;
  fit::function<void()> on_graphical_presenter_connected_;
  fit::function<void(zx_status_t)> on_graphical_presenter_error_;
  fit::function<void(fuchsia::element::ViewSpec view_spec)> on_present_view_;

 public:
  fit::function<void(std::vector<fuchsia::element::Annotation> annotations_to_set,
                     std::vector<fuchsia::element::AnnotationKey> annotations_to_delete)>
      on_update_annotations_;
  fit::function<void()> on_dismiss_;
};

}  // namespace modular_testing

#endif  // SRC_MODULAR_LIB_MODULAR_TEST_HARNESS_CPP_FAKE_GRAPHICAL_PRESENTER_H_
