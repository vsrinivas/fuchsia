// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        PresentParameters, PresentationInfo, SchedulingFuture, SchedulingFutureState, SchedulingLib,
    },
    async_trait::async_trait,
    fuchsia_async as fasync, fuchsia_trace as trace, fuchsia_zircon as zx,
    std::{
        cell::{Cell, RefCell},
        task::Waker,
    },
};

// Scheduler that tries to keep latency as low as possible and always schedules as close to latch
// time as possible.
pub struct LowLatencyScheduler {
    pub latch_offset: Cell<zx::Duration>,
    data: RefCell<WakeupData>,
    future_presentation_infos: RefCell<Vec<PresentationInfo>>,
    wait_guard: RefCell<()>,
}

// Data used to determine when to wake up. Checked as part of SchedulingFuture polling.
// Must be separate to satisfy Pin<> in SchedulingFuture.
struct WakeupData {
    frame_requested: bool,
    present_credits: u32,
    waker: Option<Waker>,
}

impl LowLatencyScheduler {
    pub fn new(latch_offset: zx::Duration) -> LowLatencyScheduler {
        LowLatencyScheduler {
            latch_offset: Cell::new(latch_offset),
            data: RefCell::new(WakeupData {
                frame_requested: false,
                present_credits: 1,
                waker: None,
            }),
            future_presentation_infos: RefCell::new(Vec::<PresentationInfo>::new()),
            wait_guard: RefCell::new(()),
        }
    }

    fn find_next_latch_and_presentation_time(&self) -> PresentationInfo {
        // Return the first applicable latch point / presentation time.
        // If we found no reasonable time, our data is probably stale so just aim for "now".
        let now = fasync::Time::now().into_zx();
        let deadline = now + self.latch_offset.get();
        self.future_presentation_infos
            .borrow()
            .iter()
            .find(|info| info.latch_point > deadline)
            .unwrap_or(&PresentationInfo { latch_point: now, presentation_time: now })
            .clone()
    }
}

#[async_trait(?Send)]
impl SchedulingLib for LowLatencyScheduler {
    fn request_present(&self) {
        let mut data = self.data.borrow_mut();
        data.frame_requested = true;
        data.maybe_wakeup();
    }

    fn on_next_frame_begin(
        &self,
        additional_present_credits: u32,
        future_presentation_infos: Vec<PresentationInfo>,
    ) {
        assert!(!future_presentation_infos.is_empty());
        self.future_presentation_infos.replace(future_presentation_infos);

        let mut data = self.data.borrow_mut();
        data.present_credits += additional_present_credits;
        data.maybe_wakeup();
    }

    async fn wait_to_update(&self) -> PresentParameters {
        // Mutably borrow the wait_guard to prevent any simultaneous waits.
        let _guard = self.wait_guard.try_borrow_mut().expect("Only one wait at a time allowed");
        // Async tracing for the waiting period
        let _trace_guard = trace::async_enter!(
            trace::generate_nonce(),
            "gfx",
            "LowLatencyScheduler::WaitForPresent"
        );

        // Wait until we want to and are able to schedule a frame.
        SchedulingFuture { sched: &self.data }.await;

        // Wait until the latest possible time to start updating.
        let next = self.find_next_latch_and_presentation_time();
        let wakeup_time = next.latch_point - self.latch_offset.get();
        fasync::Timer::new(wakeup_time).await;

        {
            // Update state for next frame.
            let mut data = self.data.borrow_mut();
            data.frame_requested = false;
            data.present_credits -= 1;
            data.waker = None;
        }

        PresentParameters {
            expected_latch_point: next.latch_point,
            expected_presentation_time: next.presentation_time,
            requested_presentation_time: zx::Time::from_nanos(0),
            unsquashable: false,
        }
    }
}

impl SchedulingFutureState for WakeupData {
    fn ready_to_wake_up(&self) -> bool {
        self.present_credits > 0 && self.frame_requested
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
        let sched = LowLatencyScheduler::new(zx::Duration::from_millis(1));
        let mut fut = sched.wait_to_update();
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Should complete after request_present() (plus a timer firing).
        sched.request_present();
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        exec.wake_next_timer();
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

    #[test]
    fn wait_after_initial_request_present_completes_after_timer() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let sched = LowLatencyScheduler::new(zx::Duration::from_millis(1));
        sched.request_present();
        let mut fut = sched.wait_to_update();
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        exec.wake_next_timer();
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

    #[test]
    fn wait_never_completes_without_present_credits() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let sched = LowLatencyScheduler::new(zx::Duration::from_millis(1));

        // Present the first frame. This depletes our initial present credit.
        sched.request_present();
        let mut fut = sched.wait_to_update();
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        exec.wake_next_timer();
        assert_matches!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(PresentParameters {
                expected_latch_point: _,
                expected_presentation_time: _,
                requested_presentation_time: _,
                unsquashable: false,
            })
        );

        // Present the second frame. There are no present credits, so it shouldn't complete.
        sched.request_present();
        let mut fut = sched.wait_to_update();
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        exec.wake_next_timer();
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        // Provide two present credits. Should now be able to present twice successfully.
        sched.on_next_frame_begin(
            2,
            vec![PresentationInfo {
                latch_point: zx::Time::from_nanos(1),
                presentation_time: zx::Time::from_nanos(1),
            }],
        );

        // The current one completes.
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        exec.wake_next_timer();
        assert_matches!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(PresentParameters {
                expected_latch_point: _,
                expected_presentation_time: _,
                requested_presentation_time: _,
                unsquashable: false,
            })
        );

        // The next one also completes.
        sched.request_present();
        let mut fut = sched.wait_to_update();
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        exec.wake_next_timer();
        assert_matches!(
            exec.run_until_stalled(&mut fut),
            Poll::Ready(PresentParameters {
                expected_latch_point: _,
                expected_presentation_time: _,
                requested_presentation_time: _,
                unsquashable: false,
            })
        );

        // But the third one doesn't, because we're out of credits again.
        sched.request_present();
        let mut fut = sched.wait_to_update();
        assert!(exec.run_until_stalled(&mut fut).is_pending());
        exec.wake_next_timer();
        assert!(exec.run_until_stalled(&mut fut).is_pending());
    }
}
