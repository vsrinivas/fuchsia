// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/sessionmgr/story_runner/story_shell_context_impl.h"

#include <fuchsia/modular/cpp/fidl.h>

#include "peridot/bin/sessionmgr/story_runner/story_controller_impl.h"
#include "peridot/bin/sessionmgr/story_runner/story_provider_impl.h"

namespace modular {

namespace {

constexpr char kStoryShellLinkName[] = "story-shell-link";

};  // namespace

StoryShellContextImpl::StoryShellContextImpl(fidl::StringPtr story_id,
                                             StoryProviderImpl* const story_provider_impl,
                                             StoryControllerImpl* const story_controller_impl)
    : story_id_(story_id),
      story_provider_impl_(story_provider_impl),
      story_controller_impl_(story_controller_impl) {}

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

void StoryShellContextImpl::GetLink(fidl::InterfaceRequest<fuchsia::modular::Link> request) {
  fuchsia::modular::LinkPathPtr link_path = fuchsia::modular::LinkPath::New();
  link_path->module_path.resize(0);
  // Note: This will never clash with link references for links owned by
  // modules, because those will have a non-empty module path.
  link_path->link_name = kStoryShellLinkName;
  story_controller_impl_->ConnectLinkPath(std::move(link_path), std::move(request));
}

void StoryShellContextImpl::RequestView(std::string surface_id) {}

void StoryShellContextImpl::OnSurfaceOffScreen(std::string surface_id) {}

}  // namespace modular
