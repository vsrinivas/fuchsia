// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/story_runner/story_shell_context_impl.h"

#include <fuchsia/modular/cpp/fidl.h>

namespace modular {

StoryShellContextImpl::StoryShellContextImpl(std::string story_id)
    : story_id_(std::move(story_id)) {}

StoryShellContextImpl::~StoryShellContextImpl() = default;

void StoryShellContextImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::StoryShellContext> request) {
  bindings_.AddBinding(this, std::move(request));
}

}  // namespace modular
