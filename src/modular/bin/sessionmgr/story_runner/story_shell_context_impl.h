// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_STORY_SHELL_CONTEXT_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_STORY_SHELL_CONTEXT_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/string.h>

namespace modular {

class StoryControllerImpl;
class StoryProviderImpl;

class StoryShellContextImpl : fuchsia::modular::StoryShellContext {
 public:
  StoryShellContextImpl(std::string story_id, StoryProviderImpl* story_provider_impl);
  ~StoryShellContextImpl() override;

  void Connect(fidl::InterfaceRequest<fuchsia::modular::StoryShellContext> request);

 private:
  // |fuchsia::modular::StoryShellContext|
  void GetPresentation(fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) override;
  void WatchVisualState(
      fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher> watcher) override;
  void RequestView(std::string surface_id) override;

  const std::string story_id_;
  // Not owned. The StoryProviderImpl corresponding to this context.
  StoryProviderImpl* const story_provider_impl_;

  fidl::BindingSet<fuchsia::modular::StoryShellContext> bindings_;
};
}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_STORY_SHELL_CONTEXT_IMPL_H_
