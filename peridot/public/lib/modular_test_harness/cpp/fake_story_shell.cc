// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/modular_test_harness/cpp/fake_story_shell.h>

namespace modular {
namespace testing {

void FakeStoryShell::OnCreate(fuchsia::sys::StartupInfo startup_info) {
  component_context()->outgoing()->AddPublicService(bindings_.GetHandler(this));
}

void FakeStoryShell::OnDestroy() {
  if (on_destroy_)
    on_destroy_();
}

fidl::InterfaceRequestHandler<fuchsia::modular::StoryShell>
FakeStoryShell::GetHandler() {
  return bindings_.GetHandler(this);
}

void FakeStoryShell::Initialize(
    fidl::InterfaceHandle<fuchsia::modular::StoryShellContext>
        story_shell_context) {
  story_shell_context_ = story_shell_context.Bind();
}

void FakeStoryShell::AddSurface(
    fuchsia::modular::ViewConnection view_connection,
    fuchsia::modular::SurfaceInfo surface_info) {
  if (on_add_surface_)
    on_add_surface_(std::move(view_connection), std::move(surface_info));
}

void FakeStoryShell::AddSurface2(
    fuchsia::modular::ViewConnection2 view_connection,
    fuchsia::modular::SurfaceInfo surface_info) {
  AddSurface(
      fuchsia::modular::ViewConnection{
          .surface_id = view_connection.surface_id,
          .view_holder_token = std::move(view_connection.view_holder_token),
      },
      std::move(surface_info));
}

}  // namespace testing
}  // namespace modular
