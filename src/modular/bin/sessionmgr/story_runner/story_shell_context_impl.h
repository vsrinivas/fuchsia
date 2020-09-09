// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_STORY_SHELL_CONTEXT_IMPL_H_
#define SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_STORY_SHELL_CONTEXT_IMPL_H_

#include <fuchsia/modular/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fidl/cpp/string.h>

namespace modular {

class StoryShellContextImpl : fuchsia::modular::StoryShellContext {
 public:
  explicit StoryShellContextImpl(std::string story_id);
  ~StoryShellContextImpl() override;

  void Connect(fidl::InterfaceRequest<fuchsia::modular::StoryShellContext> request);

 private:
  const std::string story_id_;

  fidl::BindingSet<fuchsia::modular::StoryShellContext> bindings_;
};
}  // namespace modular

#endif  // SRC_MODULAR_BIN_SESSIONMGR_STORY_RUNNER_STORY_SHELL_CONTEXT_IMPL_H_
