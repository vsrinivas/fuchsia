// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{PresentParameters, PresentationInfo, PresentedInfo, SchedulingLib},
    async_trait::async_trait,
    fuchsia_async as fasync, fuchsia_zircon as zx,
    std::cell::{Cell, RefCell},
    std::collections::{HashMap, VecDeque},
};

#[derive(Copy, Clone)]
struct Frame {
    frame_start: zx::Time,
    expected_latch_point: zx::Time,
    expected_presentation_time: zx::Time,
}

// This library is a wrapper around a SchedulingLib. It demonstrates how one can collect frame
// stats.
pub struct FrameStatsCollector<'a> {
    scheduler: &'a dyn SchedulingLib,
    total_produced: Cell<u64>,
    total_presented: Cell<u64>,
    total_dropped: Cell<u64>,
    total_delayed: Cell<u64>,
    total_missed_latch_points: Cell<u64>,
    total_latency: Cell<zx::Duration>,
    half_frame_time: Cell<zx::Duration>,
    previous_frames: RefCell<VecDeque<Frame>>,
    latency_histogram: RefCell<HashMap<zx::Duration, u64>>,
    start_time: zx::Time,
}

impl FrameStatsCollector<'_> {
    pub fn new(scheduler: &'_ dyn SchedulingLib) -> FrameStatsCollector<'_> {
        FrameStatsCollector {
            scheduler,
            total_produced: Cell::new(0),
            total_presented: Cell::new(0),
            total_dropped: Cell::new(0),
            total_delayed: Cell::new(0),
            total_missed_latch_points: Cell::new(0),
            total_latency: Cell::new(zx::Duration::from_nanos(0)),
            half_frame_time: Cell::new(zx::Duration::from_millis(4)),
            previous_frames: RefCell::new(VecDeque::new()),
            latency_histogram: RefCell::new(HashMap::new()),
            start_time: fasync::Time::now().into_zx(),
        }
    }

    // Returns the stats as a string. Example output:
    // Frame Scheduling stats:
    //        Total produced frames: 6994
    //        Total presented: 6994 (100.00%)
    //        Total dropped: 0 (0.00%)
    //        Total delayed: 2 (0.03%)
    //        Total missed latch points: 2 (0.03%)
    //        Average latency: 30.58 ms
    //        Average frame rate: 59.83 fps
    pub fn stats_to_string(&self) -> String {
        let time_since_start = fasync::Time::now().into_zx() - self.start_time;
        let average_frame_time =
            (time_since_start.into_nanos() as f64) / (self.total_presented.get() as f64);
        let average_fps = zx::Duration::from_seconds(1).into_nanos() as f64 / average_frame_time;
        let average_latency =
            (self.total_latency.get().into_millis() as f64) / (self.total_presented.get() as f64);

        format!(
            "Frame Scheduling stats:
           Total produced frames: {}
           Total presented: {} ({:.2}%)
           Total dropped: {} ({:.2}%)
           Total delayed: {} ({:.2}%)
           Total missed latch points: {} ({:.2}%)
           Average latency: {:.2} ms
           Average frame rate: {:.2} fps",
            self.total_produced.get(),
            self.total_presented.get(),
            100.0 * (self.total_presented.get() as f64) / (self.total_produced.get() as f64),
            self.total_dropped.get(),
            100.0 * (self.total_dropped.get() as f64) / (self.total_produced.get() as f64),
            self.total_delayed.get(),
            100.0 * (self.total_delayed.get() as f64) / (self.total_produced.get() as f64),
            self.total_missed_latch_points.get(),
            100.0 * (self.total_missed_latch_points.get() as f64)
                / (self.total_produced.get() as f64),
            average_latency,
            average_fps,
        )
    }
}

#[async_trait(?Send)]
impl SchedulingLib for FrameStatsCollector<'_> {
    fn request_present(&self) {
        self.scheduler.request_present()
    }

    // on_frame_presented() tells us how the previous set of presents worked out, so here we can
    // track how successful our frame scheduling is.
    fn on_frame_presented(
        &self,
        actual_presentation_time: zx::Time,
        presented_infos: Vec<PresentedInfo>,
    ) {
        let num_produced = presented_infos.len() as u64;
        let num_squashed = num_produced - 1;
        let presented_frame = self
            .previous_frames
            .borrow()
            .iter()
            .skip(num_squashed as usize)
            .next()
            .unwrap()
            .clone();

        // Updates present totals.
        self.total_produced.set(self.total_produced.get() + num_produced);
        self.total_presented.set(self.total_presented.get() + 1);
        self.total_dropped.set(self.total_dropped.get() + num_squashed);

        // Track if the current frame was shown later than expected. If so then it was "delayed", as
        // opposed to squashed presents, which are never shown and therefore "dropped".
        if actual_presentation_time
            > presented_frame.expected_presentation_time + self.half_frame_time.get()
        {
            self.total_delayed.set(self.total_delayed.get() + 1);
        }

        // Update the latency histogram with the presented frame.
        let latency = actual_presentation_time - presented_frame.frame_start;
        self.total_latency.set(self.total_latency.get() + latency);
        *self.latency_histogram.borrow_mut().entry(latency).or_insert(0) += 1;

        let previous_frames = &mut self.previous_frames.borrow_mut();
        for PresentedInfo { present_received_time: _, actual_latch_point } in presented_infos.iter()
        {
            let frame = previous_frames.pop_front().unwrap();
            if actual_latch_point > &(frame.expected_latch_point + self.half_frame_time.get()) {
                self.total_missed_latch_points.set(self.total_missed_latch_points.get() + 1);
            }
        }

        self.scheduler.on_frame_presented(actual_presentation_time, presented_infos)
    }

    fn on_next_frame_begin(
        &self,
        additional_present_credits: u32,
        future_presentation_infos: Vec<PresentationInfo>,
    ) {
        assert!(!future_presentation_infos.is_empty());
        if future_presentation_infos.len() > 1 {
            self.half_frame_time.set(
                (future_presentation_infos[1].presentation_time
                    - future_presentation_infos[0].presentation_time)
                    / 2,
            );
        }

        self.scheduler.on_next_frame_begin(additional_present_credits, future_presentation_infos)
    }

    // Tracks scheduling stats.
    async fn wait_to_update(&self) -> PresentParameters {
        let frame_start = fasync::Time::now().into_zx();
        let present_parameters = self.scheduler.wait_to_update().await;

        // Gather timestamp data before returning.
        self.previous_frames.borrow_mut().push_back(Frame {
            frame_start,
            expected_latch_point: present_parameters.expected_latch_point,
            expected_presentation_time: present_parameters.expected_presentation_time,
        });

        present_parameters
    }
}
