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

const MAX_PRESENTS_IN_FLIGHT: u32 = 3;

// Scheduler that never drops a frame. It tries to keep two presents in flight at all times for
// maximum throughput, unless there is nothing to draw (i.e. if request_present() hasn't been
// called).
//
// Notes:
// Does not track present credits. Since it's limited to at most one Present() call per
// OnNextFrameBegin() event, we're guaranteed not to run out of credits.
//
// TODO(fxbug.dev/83055): Due OnNextFrameBegin() currently only firing after acquire fences complete
// this scheduler will not manage to produce pipelined frames. This should resolve itself once
// the bug is resolved, but testing to confirm will be necessary.
pub struct UnsquashableScheduler {
    data: RefCell<WakeupData>,
    // Data used to estimate when the currently waited for frame will be displayed.
    future_presentation_infos: RefCell<Vec<PresentationInfo>>,
    last_presented_time: Cell<zx::Time>,
    // |wait_guard| is only used to enforce "one wait at a time" behavior.
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

impl UnsquashableScheduler {
    pub fn new() -> UnsquashableScheduler {
        let now = fasync_time::now().into_zx();

        UnsquashableScheduler {
            data: RefCell::new(WakeupData {
                frame_requested: false,
                next_frame_begin: true,
                waker: None,
                presents_in_flight: 0,
            }),
            future_presentation_infos: RefCell::new(vec![PresentationInfo {
                latch_point: now,
                presentation_time: now,
            }]),
            last_presented_time: Cell::new(zx::Time::from_nanos(0)),
            wait_guard: RefCell::new(()),
        }
    }

    fn estimate_presentation_time(&self) -> PresentationInfo {
        assert!(!self.future_presentation_infos.borrow().is_empty());
        let now = fasync_time::now().into_zx();
        let future_infos = self.future_presentation_infos.borrow();
        if future_infos.len() < 2 {
            return PresentationInfo { latch_point: now, presentation_time: now };
        }

        // Return the first future presentation time that is in the future and after all previously
        // presented frames (including incomplete ones).
        let frame_interval = future_infos[1].presentation_time - future_infos[0].presentation_time;
        let duration_of_frames_in_flight = zx::Duration::from_nanos(
            ((self.data.borrow().presents_in_flight as f64 + 0.5)
                * frame_interval.into_nanos() as f64) as i64,
        );
        let minimum_presentation_time =
            self.last_presented_time.get() + duration_of_frames_in_flight;
        future_infos
            .iter()
            .find(|PresentationInfo { latch_point, presentation_time }| {
                latch_point > &now && presentation_time >= &minimum_presentation_time
            })
            .unwrap_or(&PresentationInfo { latch_point: now, presentation_time: now })
            .clone()
    }
}

#[async_trait(?Send)]
impl SchedulingLib for UnsquashableScheduler {
    fn request_present(&self) {
        self.data.borrow_mut().frame_requested = true;
        self.data.borrow().maybe_wakeup();
    }

    fn on_frame_presented(
        &self,
        actual_presentation_time: zx::Time,
        _presented_infos: Vec<PresentedInfo>,
    ) {
        // Guaranteed to only be one presented, since all presents are marked "unsquashable".
        assert_eq!(_presented_infos.len(), 1);
        self.last_presented_time.set(actual_presentation_time);
        self.data.borrow_mut().presents_in_flight -= 1;
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

    async fn wait_to_update(&self) -> PresentParameters {
        // Mutably borrow the wait_guard to prevent any simultaneous waits.
        let _guard = self.wait_guard.try_borrow_mut().expect("Only one wait at a time allowed");
        // Async tracing for the waiting period.
        let _trace_guard = trace::async_enter!(
            trace::generate_nonce(),
            "gfx",
            "UnsquashableScheduler::WaitForPresent"
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

        let PresentationInfo { latch_point, presentation_time } = self.estimate_presentation_time();
        PresentParameters {
            expected_latch_point: latch_point,
            expected_presentation_time: presentation_time,
            requested_presentation_time: zx::Time::from_nanos(0),
            unsquashable: true,
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

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync, matches::assert_matches, std::task::Poll};

    #[test]
    fn wait_without_request_present_never_completes() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let sched = UnsquashableScheduler::new();
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
                unsquashable: true,
            })
        );
    }

    #[fasync::run_until_stalled(test)]
    async fn wait_after_initial_request_present_completes_immediately() {
        let sched = UnsquashableScheduler::new();
        sched.request_present();
        assert_matches!(
            sched.wait_to_update().await,
            PresentParameters {
                expected_latch_point: _,
                expected_presentation_time: _,
                requested_presentation_time: _,
                unsquashable: true,
            }
        );
    }

    #[test]
    fn following_waits_never_completes_without_on_next_frame_begin() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let sched = UnsquashableScheduler::new();
        // Initial wait always completes immediately.
        sched.request_present();
        let mut fut = sched.wait_to_update();
        exec.run_until_stalled(&mut fut).is_ready();

        // Second wait doesn't complete until after on_frame_presented().
        sched.request_present();
        let mut fut = sched.wait_to_update();
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        sched.on_next_frame_begin(
            1,
            vec![PresentationInfo {
                latch_point: zx::Time::from_nanos(1),
                presentation_time: zx::Time::from_nanos(1),
            }],
        );
        assert_matches!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(PresentParameters {
                expected_latch_point: _,
                expected_presentation_time: _,
                requested_presentation_time: _,
                unsquashable: true,
            })
        );
    }

    #[test]
    fn wait_never_completes_with_maximum_presents_in_flight() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let sched = UnsquashableScheduler::new();

        // Get MAX_PRESENTS_IN_FLIGHT started without completing any frames.
        for _ in 0..MAX_PRESENTS_IN_FLIGHT {
            sched.request_present();
            let mut fut = sched.wait_to_update();
            exec.run_until_stalled(&mut fut).is_ready();
            sched.on_next_frame_begin(
                1,
                vec![PresentationInfo {
                    latch_point: zx::Time::from_nanos(0),
                    presentation_time: zx::Time::from_nanos(0),
                }],
            );
        }

        // Next wait should not fire until on_frame_presented() has been called, reducing the number
        // of frames in flight.
        sched.request_present();
        let mut fut = sched.wait_to_update();
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        sched.on_frame_presented(
            zx::Time::from_nanos(0),
            vec![PresentedInfo {
                actual_latch_point: zx::Time::from_nanos(0),
                present_received_time: zx::Time::from_nanos(0),
            }],
        );

        assert_matches!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(PresentParameters {
                expected_latch_point: _,
                expected_presentation_time: _,
                requested_presentation_time: _,
                unsquashable: true,
            })
        );
    }
}
