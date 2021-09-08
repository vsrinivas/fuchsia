// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{PresentParameters, PresentationInfo, SchedulingLib},
    async_trait::async_trait,
    fuchsia_async::Time as fasync_time,
    fuchsia_trace as trace, fuchsia_zircon as zx,
    std::cell::Cell,
};

// Schedule a frame at each on_next_frame_begin, whether a frame has been requested or not.
//
// Note:
// Does not track present credits. Since it's limited to one Present() call per OnNextFrameBegin()
// event, we're guaranteed not to run out of credits.
pub struct NaiveScheduler {
    next_expected_times: Cell<PresentationInfo>,
    next_frame_begin: Cell<bool>,
}

impl NaiveScheduler {
    pub fn new() -> NaiveScheduler {
        let now = fasync_time::now().into_zx();
        NaiveScheduler {
            next_expected_times: Cell::new(PresentationInfo {
                latch_point: now,
                presentation_time: now,
            }),
            next_frame_begin: Cell::new(true),
        }
    }
}

#[async_trait(?Send)]
impl SchedulingLib for NaiveScheduler {
    fn on_next_frame_begin(
        &self,
        _additional_present_credits: u32,
        future_presentation_infos: Vec<PresentationInfo>,
    ) {
        assert!(!future_presentation_infos.is_empty());
        self.next_expected_times.set(future_presentation_infos[0]);
        self.next_frame_begin.set(true);
    }

    // Waits until the next on_next_frame_begin() after a request_frame().
    async fn wait_to_update(&self) -> PresentParameters {
        // Async tracing for the waiting period
        let _trace_guard =
            trace::async_enter!(trace::generate_nonce(), "gfx", "NaiveScheduler::WaitForPresent");

        // Loops until ready, yielding for 500 microseconds each loop.
        while !self.next_frame_begin.get() {
            const YIELD_TIME: zx::Duration = zx::Duration::from_micros(500);
            fuchsia_async::Timer::new(zx::Time::after(YIELD_TIME)).await;
        }

        // Reset for next frame.
        self.next_frame_begin.set(false);

        let PresentationInfo { latch_point, presentation_time } = self.next_expected_times.get();
        PresentParameters {
            expected_latch_point: latch_point,
            expected_presentation_time: presentation_time,
            requested_presentation_time: zx::Time::from_nanos(0),
            unsquashable: false,
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fuchsia_async as fasync, matches::assert_matches, std::task::Poll};

    #[fasync::run_until_stalled(test)]
    async fn first_wait_completes_immediately() {
        let sched = NaiveScheduler::new();
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
        let sched = NaiveScheduler::new();
        // Initial wait always completes immediately.
        exec.run_until_stalled(&mut sched.wait_to_update()).is_ready();
        let mut fut = sched.wait_to_update();
        assert!(exec.run_until_stalled(&mut fut).is_pending()); // Will never complete.
        exec.wake_next_timer();
        assert!(exec.run_until_stalled(&mut fut).is_pending()); // Still didn't complete.
    }

    #[fasync::run_until_stalled(test)]
    async fn on_next_frame_begin_before_wait_makes_wait_return_immediately() {
        let sched = NaiveScheduler::new();
        sched.wait_to_update().await; // Initial wait always completes immediately.
        sched.on_next_frame_begin(
            1,
            vec![PresentationInfo {
                latch_point: zx::Time::from_nanos(1),
                presentation_time: zx::Time::from_nanos(1),
            }],
        );
        assert_eq!(
            sched.wait_to_update().await,
            PresentParameters {
                expected_latch_point: zx::Time::from_nanos(1),
                expected_presentation_time: zx::Time::from_nanos(1),
                requested_presentation_time: zx::Time::from_nanos(0),
                unsquashable: false,
            }
        );
    }

    #[test]
    fn wait_completes_after_on_next_frame_begin() {
        let mut exec = fasync::TestExecutor::new().unwrap();
        let sched = NaiveScheduler::new();

        // Initial wait always completes immediately.
        let mut fut = sched.wait_to_update();
        exec.run_until_stalled(&mut fut).is_ready();

        // Next wait should not fire until on_next_frame_begin() has been called.
        let mut fut = sched.wait_to_update();
        assert!(exec.run_until_stalled(&mut fut).is_pending());

        sched.on_next_frame_begin(
            10,
            vec![PresentationInfo {
                latch_point: zx::Time::from_nanos(1),
                presentation_time: zx::Time::from_nanos(1),
            }],
        );
        exec.wake_next_timer();

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
