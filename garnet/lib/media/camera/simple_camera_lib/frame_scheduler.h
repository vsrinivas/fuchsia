// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_FRAME_SCHEDULER_H_
#define GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_FRAME_SCHEDULER_H_

#include <stdint.h>
#include <deque>
#include <mutex>
#include <thread>

#include <zircon/compiler.h>
#include <zircon/types.h>

namespace simple_camera {

// SimpleFrameScheduler determines when your next frame should be presented.
// This version of the scheduler is pretty dumb; it just schedules the frame
// a fixed time offset from the capture time.
//
// The ideal live display would put frames on the screen with the same cadence
// as they were captured, with the minimum amount of latency.  However, there
// are several factor complicating that goal:
//  1) There is a delay (Dca) between capture time and when the frame is made
//     available.  This time can vary, usually by less than a frame interval.
//  2) There is a delay (Dlead) between when the compositor is given an image,
//     and when the image can first be displayed. This delay varies based on
//     resource load.
//  3) There is a delay (Dproc) between when the frame is made available and
//     when it can be sent to the compositor.  A camera input may not need to
//     be decoded, but other video inputs may, which could lead to long delays.
//
// We can estimate the capture->display time as:
//  Dca + Dproc + Dlead + display_frame_interval * alpha
//  Where alpha is between 0 and 1.  The value of alpha depends on when
//  Tc + Dca + Dproc + Dlead falls between display times.
//
// This scheduler has three inputs:
//  - The monotonic time when a new frame is available, (Ta)
//  - The capture time of the frame (Tc)
//  - The time when the frame is presented (Tp)
//  - The scheduler also knows the time when the frame was requested to be
//    presented (Tr)
//
// To estimate the delays, we can compare Ta to Tc to get a range of Dca.
// We can clock our own processing to get Dproc.
// Dlead is a value that should be given to us from the compositor, who
// should be able to estimate it with its own measurements.
// All the delays can increase based on processor and IO loads.
//
// A 0th order approach, (done here) is just to hardcode Dca + Dproc + Dlead
// and run open loop.
// A slightly more complicated approach would be to estimate Dca and Dproc at
// runtime, and adjust the delay time appropriatly.
// For even more complexity, if Dlead is not provided from the compositor,
// Dlead can be estimated by decreasing the it until the compositor drops the
// frame.  The complication here is that we are only testing
// Dlead + display_frame_interval * alpha.  We do know when the frame is
// scheduled to be presented, so we would have to wait until alpha = 0,
// either by timing our call to the compositor, or relying on the difference
// in capture rate and display rate to present low alpha timings.  An
// additional complication with estimating Dlead is that Dlead should not be
// set as the absolute maximum of observed lead times.  Instead, a 2-sigma
// value should be used, which means enough frames must be dropped to develop
// a reasonable estimate of model of Dlead.
//
// Assuming an accurate model of the delays is achieved, The only additional
// action that could benefit a live stream is to recognize when frames need to
// be dropped (before sending to the compositor).  There are two situations
// when frames should be dropped:
//  - When the capture rate is significantly faster than the display rate.
//    This becomes a consideration when, for example, Rc > 1.5 * Rd, so every
//    third frame would be dropped.
//  - When the processing frames causes a dramatic system load, such that the
//    system cannot keep up.  This is a harder symptom to diagnose, but if
//    the estimates of Dproc + Dlead are exceeding the display rate we should
//    start dropping frames, unless the application is important enough to be
//    consuming all the system resources.
// Neither of the dropping strategies are implimented here.
//
// Additional note:
// We assume here that the capture time, Tc is in the same clock domain as
// CLOCK_MONOTONIC.  If it is not, additional work will need to be done to
// recover the transform between the device clock and CLOCK_MONOTONIC.
// Ideally we will eventually have a system where the capture device can simply
// provide access to a reference clock which can be used as one stage in the
// transformation chain.

class SimpleFrameScheduler {
 public:
  // Get the time in ns, in the domain of CLOCK_MONOTONIC when the current
  // frame should be presented.
  // @param capture_time_ns the time when the current frame was captured.
  // This function enforces that capture_time_ns is monotonically increasing,
  // so presentation times should be queried sequentially.
  uint64_t GetPresentationTimeNs(uint64_t capture_time_ns);

  // Update the scheduler that a frame has been presented.
  // @param pres_time the time in ns, in the CLOCK_MONOTONIC domain
  // when the frame was presented
  // @param pres_interval the period in ns between frame presentations
  // @param requested_pres_time the time in the CLOCK_MONOTONIC domain
  // when we requested the frame be presented.
  void OnFramePresented(uint64_t pres_time,
                        uint64_t pres_interval,
                        uint64_t requested_pres_time = 0);

 private:
  // A guess at the required lead time from when the compositor receives the
  // frame until when the frame is displayed.  From the discussion above,
  // this is Dlead + Dproc.
  static constexpr uint64_t kLeadDelayNs = ZX_MSEC(20);  // 20 ms

  // From the discussion above, this corresponds to Dca, the upper bound on the
  // delay between when the frame is captured to when the frame is available.
  static constexpr uint64_t kAcquireDelayNs = ZX_MSEC(50);  // 50 ms

  std::mutex times_lock_;
  uint64_t last_presentation_time_ns_ __TA_GUARDED(times_lock_) = 0;
  uint64_t last_capture_time_ns_ __TA_GUARDED(times_lock_) = 0;
};

}  // namespace simple_camera

#endif  // GARNET_LIB_MEDIA_CAMERA_SIMPLE_CAMERA_LIB_FRAME_SCHEDULER_H_
