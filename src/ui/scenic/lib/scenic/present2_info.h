// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCENIC_PRESENT2_INFO_H_
#define SRC_UI_SCENIC_LIB_SCENIC_PRESENT2_INFO_H_

#include <fuchsia/ui/scenic/cpp/fidl.h>
#include <lib/fit/function.h>

#include <queue>

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/scenic/forward_declarations.h"

namespace scenic_impl {

// Class used to keep track of state and logic for OnFramePresented() events corresponding to
// content submitted by Present2() calls.
//
// Every |fuchsia::ui::scenic::Present2| call creates a corresponding Present2Info object. When
// Scenic is alerted that a frame was presented, CoalescePresent2Infos() creates the
// |fuchsia::scenic::scheduling::FramePresentedInfo| object returned to the Session.
class Present2Info {
 public:
  explicit Present2Info(SessionId session_id) : session_id_(session_id) {}

  // Requires all |Present2Info|s passed in to belong to the same Session, and be in submission
  // order. Once this occurs, the vector of |present2_infos| is no longer valid.
  static fuchsia::scenic::scheduling::FramePresentedInfo CoalescePresent2Infos(
      std::vector<Present2Info> present2_infos, zx::time presentation_time);

  // Set the PresentReceivedInfo fields. These must be called exactly once per |Present2Info|.
  void SetPresentReceivedTime(zx::time present_received_time);
  void SetLatchedTime(zx::time latched_time);

  SessionId session_id() const { return session_id_; }

  // Should only be called after you have set all fields in |present_received_info_| and are done
  // using the class, for instance in CoalescePresent2Infos().
  fuchsia::scenic::scheduling::PresentReceivedInfo TakePresentReceivedInfo() {
    return std::move(present_received_info_);
  }

 private:
  SessionId session_id_;
  fuchsia::scenic::scheduling::PresentReceivedInfo present_received_info_;
};

}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_SCENIC_PRESENT2_INFO_H_
