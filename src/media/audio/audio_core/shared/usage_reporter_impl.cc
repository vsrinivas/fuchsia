// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/usage_reporter_impl.h"

#include <lib/syslog/cpp/macros.h>

namespace media::audio {

fidl::InterfaceRequestHandler<fuchsia::media::UsageReporter>
UsageReporterImpl::GetFidlRequestHandler() {
  return bindings_.GetHandler(this);
}

void UsageReporterImpl::Watch(
    fuchsia::media::Usage usage,
    fidl::InterfaceHandle<fuchsia::media::UsageWatcher> usage_state_watcher) {
  auto watcher = usage_state_watcher.Bind();
  auto& set = watcher_set(usage);
  int current_id = next_watcher_id_++;
  watcher.set_error_handler(
      [&set, current_id](zx_status_t status) { set.watchers.erase(current_id); });
  watcher->OnStateChanged(fidl::Clone(usage), fidl::Clone(set.cached_state), [current_id, &set]() {
    --set.watchers[current_id].outstanding_ack_count;
  });

  // Initialize outstanding_ack_count as 1 to count first OnStateChange message
  set.watchers[current_id] = {std::move(watcher), 1};
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

  auto it = set.watchers.begin();
  while (it != set.watchers.end()) {
    if (it->second.outstanding_ack_count > MAX_STATES) {
      FX_LOGS(INFO) << "Disconnecting unresponsive watcher";
      it = set.watchers.erase(it);
    } else {
      ++it->second.outstanding_ack_count;
      it->second.watcher_ptr->OnStateChanged(fidl::Clone(usage), fidl::Clone(state),
                                             [it]() { --it->second.outstanding_ack_count; });
      ++it;
    }
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
