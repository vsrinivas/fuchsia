// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/present2_info.h"

#include "src/lib/fxl/logging.h"

namespace scheduling {

void Present2Info::SetPresentReceivedTime(zx::time present_received_time) {
  FXL_DCHECK(!present_received_info_.has_present_received_time());
  present_received_info_.set_present_received_time(present_received_time.get());
}

void Present2Info::SetLatchedTime(zx::time latched_time) {
  FXL_DCHECK(!present_received_info_.has_latched_time());
  present_received_info_.set_latched_time(latched_time.get());
}

fuchsia::scenic::scheduling::FramePresentedInfo Present2Info::CoalescePresent2Infos(
    std::vector<Present2Info> present2_infos, zx::time presentation_time) {
  if (present2_infos.size() == 0)
    return {};

  // Should be the same for all entries in the vector.
  SessionId session_id = present2_infos[0].session_id();

  fuchsia::scenic::scheduling::FramePresentedInfo frame_presented_info = {};

  for (auto& info : present2_infos) {
    FXL_DCHECK(info.session_id() == session_id);

    auto present_received_info = info.TakePresentReceivedInfo();
    FXL_DCHECK(present_received_info.has_present_received_time());
    FXL_DCHECK(present_received_info.has_latched_time());

    frame_presented_info.presentation_infos.push_back(std::move(present_received_info));
  }

  frame_presented_info.actual_presentation_time = presentation_time.get();

  return frame_presented_info;
}

}  // namespace scheduling
