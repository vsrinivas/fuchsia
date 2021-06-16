// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_PRESENT2_HELPER_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_PRESENT2_HELPER_H_

#include <fuchsia/ui/composition/cpp/fidl.h>
#include <fuchsia/ui/gfx/cpp/fidl.h>

#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace scheduling {

// Implementation of the Present2 API functionality, desgined to be resuable for any APIs who
// want to have the same semantics.
class Present2Helper {
 public:
  explicit Present2Helper(fit::function<void(fuchsia::ui::composition::FramePresentedInfo info)>
                              on_frame_presented_event);
  ~Present2Helper() = default;

  void RegisterPresent(PresentId present_id, zx::time present_received_time);

  void OnPresented(const std::map<PresentId, zx::time>& latched_times,
                   PresentTimestamps present_times, uint64_t num_presents_allowed);

 private:
  const fit::function<void(fuchsia::ui::composition::FramePresentedInfo info)> on_frame_presented_;

  std::map<PresentId, /*present_received_time*/ zx::time> presents_received_;
};

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_PRESENT2_HELPER_H_
