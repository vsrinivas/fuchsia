// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod frame_stats_collector;
mod latency_stats_collector;
mod low_latency_scheduler;
mod naive_scheduler;
mod throughput_scheduler;
mod unsquashable_scheduler;

pub use frame_stats_collector::FrameStatsCollector;
pub use latency_stats_collector::LatencyStatsCollector;
pub use low_latency_scheduler::LowLatencyScheduler;
pub use naive_scheduler::NaiveScheduler;
pub use throughput_scheduler::ThroughputScheduler;
pub use unsquashable_scheduler::UnsquashableScheduler;

use {
    async_trait::async_trait,
    fuchsia_zircon as zx,
    std::{
        cell::RefCell,
        future::Future,
        pin::Pin,
        task::{Context, Poll, Waker},
    },
};

#[derive(Debug, PartialEq, Copy, Clone)]
pub struct PresentationInfo {
    pub latch_point: zx::Time,
    pub presentation_time: zx::Time,
}

#[derive(Debug, PartialEq, Copy, Clone)]
pub struct PresentedInfo {
    pub present_received_time: zx::Time,
    pub actual_latch_point: zx::Time,
}

#[derive(Debug, PartialEq, Copy, Clone)]
pub struct PresentParameters {
    pub expected_latch_point: zx::Time,
    pub expected_presentation_time: zx::Time,
    pub requested_presentation_time: zx::Time,
    pub unsquashable: bool,
}

#[async_trait(?Send)]
pub trait SchedulingLib {
    fn request_present(&self) {}
    fn on_frame_presented(
        &self,
        _actual_presentation_time: zx::Time,
        _presented_infos: Vec<PresentedInfo>,
    ) {
    }
    fn on_next_frame_begin(
        &self,
        additional_present_credits: u32,
        future_presentation_infos: Vec<PresentationInfo>,
    );
    async fn wait_to_update(&self) -> PresentParameters;
}

struct SchedulingFuture<'a> {
    sched: &'a RefCell<dyn SchedulingFutureState>,
}

impl Future for SchedulingFuture<'_> {
    type Output = ();
    fn poll(self: Pin<&mut Self>, ctx: &mut Context<'_>) -> Poll<()> {
        let mut sched = self.sched.borrow_mut();
        if sched.ready_to_wake_up() {
            Poll::Ready(())
        } else {
            sched.set_waker(ctx.waker().clone());
            Poll::Pending
        }
    }
}
trait SchedulingFutureState {
    fn ready_to_wake_up(&self) -> bool;
    fn set_waker(&mut self, waker: Waker);
    fn get_waker(&self) -> &Option<Waker>;
    fn maybe_wakeup(&self) {
        if let Some(waker) = &self.get_waker() {
            if self.ready_to_wake_up() {
                waker.wake_by_ref();
            }
        }
    }
}
