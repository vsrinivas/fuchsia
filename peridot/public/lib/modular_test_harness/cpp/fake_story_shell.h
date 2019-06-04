// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_MODULAR_TEST_HARNESS_CPP_FAKE_STORY_SHELL_H_
#define LIB_MODULAR_TEST_HARNESS_CPP_FAKE_STORY_SHELL_H_

#include <lib/modular_test_harness/cpp/fake_component.h>
#include <sdk/lib/sys/cpp/component_context.h>

namespace modular {
namespace testing {

// Story shell fake that provides access to the StoryShellContext.
//
// EXAMPLE USAGE (see test_harness_fixture.h for more details on how to use the
// test harness):
//
// modular::testing::FakeStoryShell fake_story_shell;
// modular::testing::TestHarnessBuilder builder;
// builder.InterceptSessionShell(fake_story_shell.GetOnCreateHandler(),
//                               {.sandbox_services = {
//                                    "fuchsia.modular.StoryShellContext"}});
//
// test_harness().events().OnNewComponent =
//     builder.BuildOnNewComponentHandler();
// test_harness()->Run(builder.BuildSpec());
//
// // Wait for the session shell to be intercepted.
// RunLoopUntil([&] { return fake_story_shell.is_running(); });
class FakeStoryShell : public modular::testing::FakeComponent,
                       fuchsia::modular::StoryShell {
 public:
  ~FakeStoryShell() override = default;

  bool is_initialized() const { return !!story_shell_context_; }

  void set_on_destroy(fit::function<void()> on_destroy) {
    on_destroy_ = std::move(on_destroy);
  }

  void set_on_add_surface(fit::function<void(fuchsia::modular::ViewConnection,
                                             fuchsia::modular::SurfaceInfo)>
                              on_add_surface) {
    on_add_surface_ = std::move(on_add_surface);
  }

  fuchsia::modular::StoryShellContext* story_shell_context() {
    return story_shell_context_.get();
  }

  // Produces a handler function that can be used in the outgoing service
  // provider.
  fidl::InterfaceRequestHandler<fuchsia::modular::StoryShell> GetHandler();

 private:
  // |modular::testing::FakeComponent|
  void OnCreate(fuchsia::sys::StartupInfo startup_info) override;

  // |modular::testing::FakeComponent|
  void OnDestroy() override;

  // |fuchsia::modular::StoryShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::StoryShellContext>
                      story_shell_context) override;

  // |fuchsia::modular::StoryShell|
  void AddSurface(fuchsia::modular::ViewConnection view_connection,
                  fuchsia::modular::SurfaceInfo surface_info) override;

  // |fuchsia::modular::StoryShell|
  // Checks if the surface relationships match expectations
  void AddSurface2(fuchsia::modular::ViewConnection2 view_connection,
                   fuchsia::modular::SurfaceInfo surface_info) override;

  // |fuchsia::modular::StoryShell|
  void FocusSurface(std::string /* surface_id */) override {}

  // |fuchsia::modular::StoryShell|
  void DefocusSurface(std::string /* surface_id */,
                      DefocusSurfaceCallback callback) override {
    callback();
  }

  // |fuchsia::modular::StoryShell|
  void AddContainer(
      std::string /* container_name */, fidl::StringPtr /* parent_id */,
      fuchsia::modular::SurfaceRelation /* relation */,
      std::vector<fuchsia::modular::ContainerLayout> /* layout */,
      std::vector<fuchsia::modular::ContainerRelationEntry> /* relationships */,
      std::vector<fuchsia::modular::ContainerView> /* views */) override {}

  // |fuchsia::modular::StoryShell|
  void RemoveSurface(std::string /* surface_id */) override {}

  // |fuchsia::modular::StoryShell|
  void ReconnectView(
      fuchsia::modular::ViewConnection view_connection) override {}

  // |fuchsia::modular::StoryShell|
  void UpdateSurface(
      fuchsia::modular::ViewConnection view_connection,
      fuchsia::modular::SurfaceInfo /* surface_info */) override {}

  fuchsia::modular::StoryShellContextPtr story_shell_context_;
  fidl::BindingSet<fuchsia::modular::StoryShell> bindings_;
  fit::function<void(fuchsia::modular::ViewConnection,
                     fuchsia::modular::SurfaceInfo)>
      on_add_surface_;
  fit::function<void()> on_destroy_;
};

}  // namespace testing
}  // namespace modular

#endif  // LIB_MODULAR_TEST_HARNESS_CPP_FAKE_STORY_SHELL_H_
