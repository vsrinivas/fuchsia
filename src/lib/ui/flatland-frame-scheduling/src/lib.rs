// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod throughput_scheduler;

pub use throughput_scheduler::ThroughputScheduler;

use {
    async_trait::async_trait,
    fidl_fuchsia_scenic_scheduling as frame_scheduling, fidl_fuchsia_ui_composition as flatland,
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

impl From<frame_scheduling::PresentReceivedInfo> for PresentedInfo {
    fn from(item: frame_scheduling::PresentReceivedInfo) -> PresentedInfo {
        PresentedInfo {
            present_received_time: zx::Time::from_nanos(item.present_received_time.unwrap()),
            actual_latch_point: zx::Time::from_nanos(item.latched_time.unwrap()),
        }
    }
}

#[derive(Debug, PartialEq, Copy, Clone)]
pub struct PresentParameters {
    // The latch point we're expecting to make for this update.
    pub expected_latch_point: zx::Time,
    // The time we're expecting to be presented to the display.
    pub expected_presentation_time: zx::Time,
    // The requested_presentation_time to pass into Present().
    pub requested_presentation_time: zx::Time,
    // The unsquashable boolean to pass into Present().
    pub unsquashable: bool,
}

impl From<PresentParameters> for flatland::PresentArgs {
    fn from(item: PresentParameters) -> flatland::PresentArgs {
        flatland::PresentArgs {
            requested_presentation_time: Some(item.requested_presentation_time.into_nanos()),
            unsquashable: Some(item.unsquashable),
            ..flatland::PresentArgs::EMPTY
        }
    }
}

#[async_trait(?Send)]
pub trait SchedulingLib {
    // Called whenever the client has new content that it wants displayed.
    // Eventually results in wait_to_update() resolving.
    fn request_present(&self) {}
    // Should be called whenever the OnFramePresented event is received.
    fn on_frame_presented(
        &self,
        _actual_presentation_time: zx::Time,
        _presented_infos: Vec<PresentedInfo>,
    ) {
    }
    // Should be called whenever the OnNextFrameBegin event is received.
    fn on_next_frame_begin(
        &self,
        additional_present_credits: u32,
        future_presentation_infos: Vec<PresentationInfo>,
    );
    // Async call that resolves when the caller should render/apply updates and call Present.
    // Caller should always be waiting on wait_to_update() to resolve.
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
