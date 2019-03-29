// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_TESTS_STORY_SHELL_FACTORY_STORY_SHELL_IMPL_H_
#define PERIDOT_TESTS_STORY_SHELL_FACTORY_STORY_SHELL_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/macros.h>

namespace modular {
namespace testing {

// An implementation of the fuchsia.modular.StoryShell FIDL service, to be
// used in integration tests.
class StoryShellImpl : fuchsia::modular::StoryShell {
 public:
  StoryShellImpl();
  ~StoryShellImpl() override;

  // Produces a handler function that can be used in the outgoing service
  // provider.
  fidl::InterfaceRequestHandler<fuchsia::modular::StoryShell> GetHandler();

 private:
  // |fuchsia::modular::StoryShell|
  void Initialize(fidl::InterfaceHandle<fuchsia::modular::StoryShellContext>
                      story_shell_context) override {}

  // |fuchsia::modular::StoryShell|
  void AddSurface(fuchsia::modular::ViewConnection view_connection,
                  fuchsia::modular::SurfaceInfo surface_info) override {}

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

  fidl::BindingSet<fuchsia::modular::StoryShell> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StoryShellImpl);
};

}  // namespace testing
}  // namespace modular

#endif  // PERIDOT_TESTS_STORY_SHELL_FACTORY_STORY_SHELL_IMPL_H_
