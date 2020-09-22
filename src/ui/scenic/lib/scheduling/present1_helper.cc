// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/present1_helper.h"

#include <lib/trace/event.h>

namespace scheduling {

void Present1Helper::RegisterPresent(PresentId present_id, OnFramePresentedCallback callback) {
  // |present_id|s must be ordered and cannot be reused.
  FX_DCHECK(callbacks_.empty() || callbacks_.rbegin()->first < present_id);
  callbacks_.emplace(present_id, std::move(callback));
}

void Present1Helper::OnPresented(const std::map<PresentId, zx::time>& latched_times,
                                 PresentTimestamps present_times) {
  FX_DCHECK(!latched_times.empty());

  const PresentId last_present_id = latched_times.rbegin()->first;
  FX_DCHECK(callbacks_.count(last_present_id));
  SignalCallbacksUpTo(last_present_id, present_times);
}

void Present1Helper::SignalCallbacksUpTo(PresentId present_id, PresentTimestamps present_times) {
  auto presentation_info = fuchsia::images::PresentationInfo();
  presentation_info.presentation_time = present_times.presented_time.get();
  presentation_info.presentation_interval = present_times.vsync_interval.get();

  auto begin_it = callbacks_.lower_bound(0);
  auto end_it = callbacks_.upper_bound(present_id);
  FX_DCHECK(std::distance(begin_it, end_it) >= 0);
  std::for_each(begin_it, end_it,
                [presentation_info](std::pair<const PresentId, OnFramePresentedCallback>& pair) {
                  // TODO(fxbug.dev/24540): Make this unique per session via id().
                  TRACE_FLOW_BEGIN("gfx", "present_callback", presentation_info.presentation_time);
                  auto& callback = pair.second;
                  callback(presentation_info);
                });
  callbacks_.erase(begin_it, end_it);
}

}  // namespace scheduling
