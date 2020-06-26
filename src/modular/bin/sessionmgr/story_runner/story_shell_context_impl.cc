// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/modular/bin/sessionmgr/story_runner/story_shell_context_impl.h"

#include <fuchsia/modular/cpp/fidl.h>

#include "src/modular/bin/sessionmgr/story_runner/story_provider_impl.h"

namespace modular {

StoryShellContextImpl::StoryShellContextImpl(std::string story_id,
                                             StoryProviderImpl* const story_provider_impl)
    : story_id_(story_id), story_provider_impl_(story_provider_impl) {}

StoryShellContextImpl::~StoryShellContextImpl() = default;

void StoryShellContextImpl::Connect(
    fidl::InterfaceRequest<fuchsia::modular::StoryShellContext> request) {
  bindings_.AddBinding(this, std::move(request));
}

void StoryShellContextImpl::GetPresentation(
    fidl::InterfaceRequest<fuchsia::ui::policy::Presentation> request) {
  story_provider_impl_->GetPresentation(story_id_, std::move(request));
}

void StoryShellContextImpl::WatchVisualState(
    fidl::InterfaceHandle<fuchsia::modular::StoryVisualStateWatcher> watcher) {
  story_provider_impl_->WatchVisualState(story_id_, std::move(watcher));
}

void StoryShellContextImpl::RequestView(std::string surface_id) {}

}  // namespace modular
