// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/tests/story_shell_factory/story_shell_impl.h"

namespace modular {
namespace testing {

StoryShellImpl::StoryShellImpl() = default;
StoryShellImpl::~StoryShellImpl() = default;

fidl::InterfaceRequestHandler<fuchsia::modular::StoryShell>
StoryShellImpl::GetHandler() {
  return bindings_.GetHandler(this);
}

}  // namespace testing
}  // namespace modular
