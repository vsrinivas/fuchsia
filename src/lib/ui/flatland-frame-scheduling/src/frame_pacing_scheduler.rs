// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        PresentParameters, PresentationInfo, PresentedInfo, SchedulingFuture,
        SchedulingFutureState, SchedulingLib,
    },
    async_trait::async_trait,
    fuchsia_async::Time as fasync_time,
    fuchsia_trace as trace, fuchsia_zircon as zx,
    std::cell::{Cell, RefCell},
    std::task::Waker,
};

const MAX_PRESENTS_IN_FLIGHT: u32 = 2;
// How close to a cleanly divisible frame rate before we assume the difference is a rounding error.
const FRAME_RATE_EPSILON: f64 = 0.1;

// Scheduler for achieving a specific frame rate.
// For frame rates that cleanly divide the display refresh rate, frames are simply produced at the
// appropriate interval (i.e. 30 fps on 60 Hz refresh means each frame is presented for two refresh
// intervals). For frame rates that don't evenly divide the refresh rate an approximation is made by
// using the integer part of the division as the display interval and then adding up the fractional
// part over a series of frames until it adds up to a full frame, at which point we wait an
// additional frame. For example: for 24 fps on a 60Hz refresh we end up with something equivalent
// to a 2:3 pulldown sequence (https://en.wikipedia.org/wiki/Three-two_pull_down)).
//
// Notes:
// Does not track present credits. Since it's limited to at most one Present() call per
// OnNextFrameBegin() event, we're guaranteed not to run out of credits.
//
// TODO(fxbug.dev/83055): Due OnNextFrameBegin() currently only firing after acquire fences complete
// this scheduler will not manage to produce pipelined frames. This should resolve itself once
// the bug is resolved, but testing to confirm will be necessary.
pub struct FramePacingScheduler {
    target_fps: u32,
    data: RefCell<WakeupData>,
    future_presentation_infos: RefCell<Vec<PresentationInfo>>,
    last_presentation_time: Cell<Option<zx::Time>>,
    current_fractional: Cell<f64>,
    wait_guard: RefCell<()>,
}

// Data used to determine when to wake up. Checked as part of SchedulingFuture polling.
// Must be separate to satisfy Pin<> in SchedulingFuture.
struct WakeupData {
    frame_requested: bool,
    next_frame_begin: bool,
    presents_in_flight: u32,
    waker: Option<Waker>,
}

impl FramePacingScheduler {
    pub fn new(target_fps: u32) -> FramePacingScheduler {
        FramePacingScheduler {
            target_fps,
            data: RefCell::new(WakeupData {
                frame_requested: false,
                next_frame_begin: true,
                presents_in_flight: 0,
                waker: None,
            }),
            future_presentation_infos: RefCell::new(Vec::new()),
            last_presentation_time: Cell::new(None),
            current_fractional: Cell::new(0.0),
            wait_guard: RefCell::new(()),
        }
    }

    // Try to get presentation times to match.
    fn get_next_presentation_time(&self) -> PresentationInfo {
        let now = fasync_time::now().into_zx();
        if self.future_presentation_infos.borrow().len() > 1 {
            if let Some(last_presentation_time) = self.last_presentation_time.get() {
                let frame_duration = self.future_presentation_infos.borrow()[1].presentation_time
                    - self.future_presentation_infos.borrow()[0].presentation_time;
                let native_fps =
                    zx::Duration::from_seconds(1).into_nanos() / frame_duration.into_nanos();
                let frames_per_frame = (native_fps as f64) / (self.target_fps as f64);

                let frames_to_wait =
                    if (frames_per_frame - frames_per_frame.round()).abs() < FRAME_RATE_EPSILON {
                        // If the difference from cleanly divisible frame rate is too small, just
                        // assume a rounding error and try to get exact frame rate.
                        frames_per_frame.round() as i64
                    } else {
                        // We always wait at least the integer part of |frames_per_frame|.
                        let mut frames_to_wait = frames_per_frame.trunc() as i64;
                        if frames_to_wait > 0 {
                            // Every time we've had enough frames to push the fractional over the limit,
                            // wait another frame to catch up.
                            self.current_fractional
                                .set(self.current_fractional.get() + frames_per_frame.fract());
                            if self.current_fractional.get() >= 1.0 {
                                self.current_fractional.set(self.current_fractional.get().fract());
                                frames_to_wait += 1;
                            }
                        }
                        frames_to_wait
                    };

                // Find the next closest presentation time to the target presentation time.
                let half_frame = zx::Duration::from_nanos(frame_duration.into_nanos() / 2);
                let target_presentation_time = last_presentation_time
                    + zx::Duration::from_nanos(frames_to_wait * frame_duration.into_nanos());
                return self
                    .future_presentation_infos
                    .borrow()
                    .iter()
                    .find(|PresentationInfo { latch_point, presentation_time }| {
                        // Subtract half a frame from target_presentation_time to avoid going over
                        // the closest presentation time.
                        latch_point > &now
                            && presentation_time > &(target_presentation_time - half_frame)
                    })
                    .unwrap_or(&PresentationInfo {
                        latch_point: target_presentation_time - half_frame,
                        presentation_time: target_presentation_time,
                    })
                    .clone();
            }
        }

        // else
        PresentationInfo { latch_point: now, presentation_time: now }
    }
}

#[async_trait(?Send)]
impl SchedulingLib for FramePacingScheduler {
    fn request_present(&self) {
        self.data.borrow_mut().frame_requested = true;
        self.data.borrow().maybe_wakeup();
    }

    fn on_frame_presented(
        &self,
        _actual_presentation_time: zx::Time,
        presented_infos: Vec<PresentedInfo>,
    ) {
        self.data.borrow_mut().presents_in_flight -= presented_infos.len() as u32;
        self.data.borrow().maybe_wakeup();
    }

    fn on_next_frame_begin(
        &self,
        _additional_present_credits: u32,
        future_presentation_infos: Vec<PresentationInfo>,
    ) {
        assert!(!future_presentation_infos.is_empty());
        self.future_presentation_infos.replace(future_presentation_infos);
        self.data.borrow_mut().next_frame_begin = true;
        self.data.borrow().maybe_wakeup();
    }

    // Waits until the next on_next_frame_begin() after a request_frame().
    async fn wait_to_update(&self) -> PresentParameters {
        // Mutably borrow the wait_guard to prevent any simultaneous waits.
        let _guard = self.wait_guard.try_borrow_mut().expect("Only one wait at a time allowed");
        // Async tracing for the waiting period
        let _trace_guard = trace::async_enter!(
            trace::generate_nonce(),
            "gfx",
            "FramePacingScheduler::WaitForPresent"
        );

        // Wait until we're ready to draw.
        SchedulingFuture { sched: &self.data }.await;

        {
            // Update state for next frame.
            let mut data = self.data.borrow_mut();
            data.frame_requested = false;
            data.next_frame_begin = false;
            data.presents_in_flight += 1;
            data.waker = None;
        }

        let PresentationInfo { latch_point, presentation_time } = self.get_next_presentation_time();
        self.last_presentation_time.set(Some(presentation_time));
        let half_frame = if self.future_presentation_infos.borrow().len() > 1 {
            let frame_duration = self.future_presentation_infos.borrow()[1].presentation_time
                - self.future_presentation_infos.borrow()[0].presentation_time;
            zx::Duration::from_nanos(frame_duration.into_nanos() / 2)
        } else {
            zx::Duration::from_millis(4)
        };
        PresentParameters {
            expected_latch_point: latch_point,
            expected_presentation_time: presentation_time,
            // Subtract half a frame from the presentation time to avoid issues around vsync drift.
            requested_presentation_time: presentation_time - half_frame,
            unsquashable: false,
        }
    }
}

impl SchedulingFutureState for WakeupData {
    fn ready_to_wake_up(&self) -> bool {
        self.frame_requested
            && self.next_frame_begin
            && self.presents_in_flight < MAX_PRESENTS_IN_FLIGHT
    }

    fn set_waker(&mut self, waker: Waker) {
        self.waker = Some(waker);
    }

    fn get_waker(&self) -> &Option<Waker> {
        &self.waker
    }
}
