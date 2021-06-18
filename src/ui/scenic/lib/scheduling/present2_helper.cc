// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/present2_helper.h"

#include <lib/async/default.h>
#include <lib/async/time.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace scheduling {

Present2Helper::Present2Helper(
    fit::function<void(fuchsia::scenic::scheduling::FramePresentedInfo info)>
        on_frame_presented_event)
    : on_frame_presented_(std::move(on_frame_presented_event)) {
  FX_DCHECK(on_frame_presented_);
}

void Present2Helper::RegisterPresent(PresentId present_id, zx::time present_received_time) {
  FX_DCHECK(presents_received_.empty() || presents_received_.rbegin()->first < present_id)
      << "present_ids must be strictly increasing";
  presents_received_.emplace(present_id, present_received_time);
}

void Present2Helper::OnPresented(const std::map<PresentId, zx::time>& latched_times,
                                 PresentTimestamps present_times, uint64_t num_presents_allowed) {
  FX_DCHECK(!latched_times.empty());

  // Add present information of all handled presents to output.
  fuchsia::scenic::scheduling::FramePresentedInfo frame_presented_info = {};
  frame_presented_info.actual_presentation_time = present_times.presented_time.get();
  frame_presented_info.num_presents_allowed = num_presents_allowed;
  for (const auto& [present_id, latched_time] : latched_times) {
    fuchsia::scenic::scheduling::PresentReceivedInfo info;
    info.set_latched_time(latched_time.get());
    FX_DCHECK(presents_received_.count(present_id));
    info.set_present_received_time(presents_received_[present_id].get());
    frame_presented_info.presentation_infos.emplace_back(std::move(info));
  }

  // Erase all presents up to |last_present_id|.
  const PresentId last_present_id = latched_times.rbegin()->first;
  presents_received_.erase(presents_received_.begin(),
                           presents_received_.upper_bound(last_present_id));
  FX_DCHECK(presents_received_.empty() || presents_received_.begin()->first > last_present_id);

  // Invoke the Session's OnFramePresented event.
  TRACE_FLOW_BEGIN("gfx", "present_callback", present_times.presented_time.get());
  on_frame_presented_(std::move(frame_presented_info));
}

}  // namespace scheduling
