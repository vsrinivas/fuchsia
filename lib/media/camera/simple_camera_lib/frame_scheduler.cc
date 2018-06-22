// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <garnet/lib/media/camera/simple_camera_lib/frame_scheduler.h>
#include <lib/fxl/command_line.h>
#include <zircon/syscalls.h>

namespace simple_camera {

uint64_t SimpleFrameScheduler::GetPresentationTimeNs(uint64_t capture_time_ns) {
  std::lock_guard<std::mutex> lck(times_lock_);
  uint64_t now_ns = zx_clock_get(ZX_CLOCK_MONOTONIC);

  if (now_ns < capture_time_ns) {
      FXL_LOG(WARNING) << "Capture time is in the future!"
      << " now: " << now_ns << " capture_time_ns " << capture_time_ns;
      capture_time_ns = now_ns;
  }

  if (last_capture_time_ns_ > capture_time_ns) {
    FXL_LOG(ERROR) << "Capture times out of sequence! Previous: "
                   << last_capture_time_ns_
                   << " Current capture time: " << capture_time_ns;
    // Just update the capture time. Previous time cannot be in future...
    capture_time_ns = last_capture_time_ns_;
  }
  last_capture_time_ns_ = capture_time_ns;

  // To keep the frame rate as smooth as possible, we want to present in
  // reference to the capture time:
  // presentation_time = capture_time_ns + kLeadDelayNs + kAcquireDelayNs
  uint64_t pres_time = capture_time_ns + kLeadDelayNs + kAcquireDelayNs;

  // But if the delay is too long between capture and acquire, we're going
  // to miss our deadline.  In that case, the best thing we can do is just
  // add the lead delay, and send it out:
  if (now_ns - capture_time_ns > kAcquireDelayNs) {
    FXL_LOG(WARNING) << "Unexpected delay between capture and availability!"
                     << " now_ns - capture_time_ns = "
                     << now_ns - capture_time_ns << " > AcquireDelay ("
                     << kAcquireDelayNs << ")";
    pres_time = now_ns + kLeadDelayNs;
  }

  // The consumer requires that all presentation times be monotonic.
  // At this point, it should be impossible, since capture_time_ns and
  // now_ns are monotonic.  But we check here incase the logic above
  // changes.
  FXL_DCHECK(last_presentation_time_ns_ <= pres_time)
      << "Presentation "
      << "time decreased! Previous: " << last_presentation_time_ns_
      << " Current presentation time: " << pres_time;

  last_presentation_time_ns_ = pres_time;
  return pres_time;
}

// Currently this function only exists as a stub, which complains if we
// are not meeting our assumptions. It could be augmented to
// keep an estimate of the lead time required by the compositor. See this
// class's header file for more information.
void SimpleFrameScheduler::OnFramePresented(uint64_t pres_time,
                                            uint64_t pres_interval,
                                            uint64_t requested_pres_time) {
  if (pres_time > requested_pres_time + pres_interval) {
    // This means we missed the frame we were targetting.
    // Eventually, we will use this information to update Dlead.
    FXL_LOG(WARNING) << "Missed rendering deadline!"
                     << "\n  Requested   time: " << requested_pres_time
                     << "\n  Actual pres time: " << pres_time
                     << "\n  Difference:       "
                     << pres_time - requested_pres_time
                     << " > presentation_interval (" << pres_interval << ")";
  }
}

}  // namespace simple_camera
