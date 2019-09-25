// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/usage_reporter_impl.h"

namespace media::audio {

fidl::InterfaceRequestHandler<fuchsia::media::UsageReporter> UsageReporterImpl::GetHandler() {
  return bindings_.GetHandler(this);
}

void UsageReporterImpl::Watch(
    fuchsia::media::Usage usage,
    fidl::InterfaceHandle<fuchsia::media::UsageWatcher> usage_state_watcher) {
  auto watcher = usage_state_watcher.Bind();
  auto& set = watcher_set(usage);
  watcher->OnStateChanged(fidl::Clone(usage), fidl::Clone(set.cached_state), []() {
    // TODO(37214): Implement per-client queues for flow control
  });

  set.watchers.push_back(std::move(watcher));
}

void UsageReporterImpl::ReportPolicyAction(fuchsia::media::Usage usage,
                                           fuchsia::media::Behavior policy_action) {
  const auto state = [&policy_action] {
    fuchsia::media::UsageState usage_state;
    if (policy_action == fuchsia::media::Behavior::NONE) {
      usage_state.set_unadjusted({});
    } else if (policy_action == fuchsia::media::Behavior::DUCK) {
      usage_state.set_ducked({});
    } else {
      usage_state.set_muted({});
    }
    return usage_state;
  }();

  auto& set = watcher_set(usage);
  set.cached_state = fidl::Clone(state);

  for (auto& watcher : set.watchers) {
    watcher->OnStateChanged(fidl::Clone(usage), fidl::Clone(state), []() {
      // TODO(37214): Implement per-client queues for flow control
    });
  }
}

UsageReporterImpl::WatcherSet& UsageReporterImpl::watcher_set(const fuchsia::media::Usage& usage) {
  if (usage.is_render_usage()) {
    return render_usage_watchers_[fidl::ToUnderlying(usage.render_usage())];
  } else {
    return capture_usage_watchers_[fidl::ToUnderlying(usage.capture_usage())];
  }
}

}  // namespace media::audio
