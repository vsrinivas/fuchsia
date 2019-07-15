// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_SHELL_CONTEXT_IMPL_H_
#define PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_SHELL_CONTEXT_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/string.h>

namespace modular {

class StoryControllerImpl;
class StoryProviderImpl;

class StoryShellContextImpl : fuchsia::modular::StoryShellContext {
 public:
  StoryShellContextImpl(fidl::StringPtr story_id, StoryProviderImpl* story_provider_impl,
                        StoryControllerImpl* story_controller_impl);
  ~StoryShellContextImpl() override;

  void Connect(fidl::InterfaceRequest<fuchsia::modular::StoryShellContext> request);

 private:
  // |fuchsia::modular::StoryShellContext|
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) override;
  void WatchVisualState(
      fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher> watcher) override;
  void GetLink(fidl::InterfaceRequest<fuchsia::modular::Link> request) override;
  void RequestView(std::string surface_id) override;
  void OnSurfaceOffScreen(std::string surface_id) override;

  const fidl::StringPtr story_id_;
  // Not owned. The StoryProviderImpl corresponding to this context.
  StoryProviderImpl* const story_provider_impl_;
  // Not owned. The StoryControllerImpl for the Story corresponding to this
  // context.
  StoryControllerImpl* const story_controller_impl_;

  fidl::BindingSet<fuchsia::modular::StoryShellContext> bindings_;
};
}  // namespace modular

#endif  // PERIDOT_BIN_SESSIONMGR_STORY_RUNNER_STORY_SHELL_CONTEXT_IMPL_H_
