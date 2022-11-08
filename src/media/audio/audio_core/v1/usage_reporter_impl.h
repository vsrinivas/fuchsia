// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_MEDIA_AUDIO_AUDIO_CORE_V1_USAGE_REPORTER_IMPL_H_
#define SRC_MEDIA_AUDIO_AUDIO_CORE_V1_USAGE_REPORTER_IMPL_H_

#include <fuchsia/media/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <vector>

#include "src/media/audio/audio_core/shared/audio_admin.h"

namespace media::audio {

class UsageReporterImpl : public AudioAdmin::PolicyActionReporter,
                          public fuchsia::media::UsageReporter {
 public:
  fidl::InterfaceRequestHandler<fuchsia::media::UsageReporter> GetFidlRequestHandler();

 private:
  struct Watcher {
    fuchsia::media::UsageWatcherPtr watcher_ptr;
    int outstanding_ack_count;
  };

  struct WatcherSet {
    std::map<int, Watcher> watchers;
    fuchsia::media::UsageState cached_state = fuchsia::media::UsageState::WithUnadjusted({});
  };

  void Watch(fuchsia::media::Usage usage,
             fidl::InterfaceHandle<fuchsia::media::UsageWatcher> usage_state_watcher) override;

  void ReportPolicyAction(fuchsia::media::Usage usage,
                          fuchsia::media::Behavior policy_action) override;

  WatcherSet& watcher_set(const fuchsia::media::Usage& usage);

  fidl::BindingSet<fuchsia::media::UsageReporter, UsageReporterImpl*> bindings_;
  std::array<WatcherSet, fuchsia::media::RENDER_USAGE_COUNT> render_usage_watchers_;
  std::array<WatcherSet, fuchsia::media::CAPTURE_USAGE_COUNT> capture_usage_watchers_;
  int next_watcher_id_ = 0;

  // Maximum number of states that can go un-acked before a watcher is disconnected
  static const int MAX_STATES = 20;

  friend class UsageReporterImplTest;
};

}  // namespace media::audio

#endif  // SRC_MEDIA_AUDIO_AUDIO_CORE_V1_USAGE_REPORTER_IMPL_H_
