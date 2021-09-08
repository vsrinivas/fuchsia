// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        PresentParameters, PresentationInfo, SchedulingFuture, SchedulingFutureState, SchedulingLib,
    },
    async_trait::async_trait,
    fuchsia_async::Time as fasync_time,
    fuchsia_trace as trace, fuchsia_zircon as zx,
    std::cell::{Cell, RefCell},
    std::task::Waker,
};

// Scheduler for maximum throughput. Tries to schedule a frame at each on_next_frame_begin, if
// there's something to draw (i.e. request_present() has been called). Presents are always
// squashable, so if scenic misses a deadline the frame may be dropped.
//
// Notes:
// Does not track present credits. Since it's limited to at most one Present() call per
// OnNextFrameBegin() event, we're guaranteed not to run out of credits.
//
// TODO(fxbug.dev/83055): Due OnNextFrameBegin() currently only firing after acquire fences complete
// this scheduler will not manage to produce pipelined frames. This should resolve itself once
// the bug is resolved, but testing to confirm will be necessary.
pub struct ThroughputScheduler {
    data: RefCell<WakeupData>,
    next_expected_times: Cell<PresentationInfo>,
    wait_guard: RefCell<()>,
}

// Data used to determine when to wake up. Checked as part of SchedulingFuture polling.
// Must be separate to satisfy Pin<> in SchedulingFuture.
struct WakeupData {
    frame_requested: bool,
    next_frame_begin: bool,
    waker: Option<Waker>,
}

impl ThroughputScheduler {
    pub fn new() -> ThroughputScheduler {
        let now = fasync_time::now().into_zx();
        ThroughputScheduler {
            data: RefCell::new(WakeupData {
                frame_requested: false,
                next_frame_begin: true,
                waker: None,
            }),
            next_expected_times: Cell::new(PresentationInfo {
                latch_point: now,
                presentation_time: now,
            }),
            wait_guard: RefCell::new(()),
        }
    }
}

#[async_trait(?Send)]
impl SchedulingLib for ThroughputScheduler {
    fn request_present(&self) {
        self.data.borrow_mut().frame_requested = true;
        self.data.borrow().maybe_wakeup();
    }

    fn on_next_frame_begin(
        &self,
        _additional_present_credits: u32,
        future_presentation_infos: Vec<PresentationInfo>,
    ) {
        assert!(!future_presentation_infos.is_empty());
        self.next_expected_times.set(future_presentation_infos[0]);
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
            "ThroughputScheduler::WaitForPresent"
        );

        // Wait until we're ready to draw.
        SchedulingFuture { sched: &self.data }.await;

        {
            // Update state for next frame.
            let mut data = self.data.borrow_mut();
            data.frame_requested = false;
            data.next_frame_begin = false;
            data.waker = None;
        }

        let PresentationInfo { latch_point, presentation_time } = self.next_expected_times.get();
        PresentParameters {
            expected_latch_point: latch_point,
            expected_presentation_time: presentation_time,
            requested_presentation_time: zx::Time::from_nanos(0),
            unsquashable: false,
        }
    }
}

impl SchedulingFutureState for WakeupData {
    fn ready_to_wake_up(&self) -> bool {
        self.frame_requested && self.next_frame_begin
    }

    fn set_waker(&mut self, waker: Waker) {
        self.waker = Some(waker);
    }

    fn get_waker(&self) -> &Option<Waker> {
        &self.waker
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync, matches::assert_matches, std::task::Poll};

    #[test]
    fn wait_without_request_present_never_completes() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let sched = ThroughputScheduler::new();
        let mut fut = sched.wait_to_update();
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Should complete after request_present().
        sched.request_present();
        assert_matches!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(PresentParameters {
                expected_latch_point: _,
                expected_presentation_time: _,
                requested_presentation_time: _,
                unsquashable: false,
            })
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn wait_after_initial_request_present_completes_immediately() {
        let sched = ThroughputScheduler::new();
        sched.request_present();
        assert_matches!(
            sched.wait_to_update().await,
            PresentParameters {
                expected_latch_point: _,
                expected_presentation_time: _,
                requested_presentation_time: _,
                unsquashable: false,
            }
        );
    }

    #[test]
    fn following_waits_never_completes_without_on_next_frame_begin() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let sched = ThroughputScheduler::new();
        // Initial wait always completes immediately.
        sched.request_present();
        let mut fut = sched.wait_to_update();
        exec.run_until_stalled(&mut fut).is_ready();

        // Second wait doesn't complete until after on_frame_presented().
        sched.request_present();
        let mut fut = sched.wait_to_update();
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        sched.on_next_frame_begin(
            10,
            vec![PresentationInfo {
                latch_point: zx::Time::from_nanos(1),
                presentation_time: zx::Time::from_nanos(1),
            }],
        );
        assert_eq!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(PresentParameters {
                expected_latch_point: zx::Time::from_nanos(1),
                expected_presentation_time: zx::Time::from_nanos(1),
                requested_presentation_time: zx::Time::from_nanos(0),
                unsquashable: false,
            })
        );
    }
}
