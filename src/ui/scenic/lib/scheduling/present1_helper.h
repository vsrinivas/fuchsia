// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_PRESENT1_HELPER_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_PRESENT1_HELPER_H_

#include <fuchsia/ui/gfx/cpp/fidl.h>

#include "src/ui/scenic/lib/scheduling/frame_scheduler.h"
#include "src/ui/scenic/lib/scheduling/id.h"

namespace scheduling {

// Implementation of the Present1 API functionality, desgined to be resuable for any new APIs who
// want to have the same semantics.
class Present1Helper {
 public:
  using OnFramePresentedCallback = fit::function<void(fuchsia::images::PresentationInfo)>;

  Present1Helper() = default;
  ~Present1Helper() = default;

  void RegisterPresent(PresentId present_id, OnFramePresentedCallback callback);

  void OnPresented(const std::map<PresentId, zx::time>& latched_times,
                   PresentTimestamps present_times);

 private:
  // Signal all callbacks up to |present_id|.
  void SignalCallbacksUpTo(PresentId present_id, PresentTimestamps presented_time);

  std::map<PresentId, OnFramePresentedCallback> callbacks_;
};

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_PRESENT1_HELPER_H_
