// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        FrameStatsCollector, LowLatencyScheduler, PresentParameters, PresentationInfo,
        PresentedInfo, SchedulingLib,
    },
    async_trait::async_trait,
    fuchsia_zircon as zx,
    std::cell::{Cell, RefCell},
    std::collections::{HashMap, VecDeque},
};

#[derive(Copy, Clone)]
struct FrameCount {
    total: u64,
    missed_latch_points: u64,
}

#[derive(Copy, Clone)]
struct Frame {
    latch_offset: zx::Duration,
    expected_latch_point: zx::Time,
}

// This library is a wrapper around LowLatencyScheduler and FrameStatsCollector. It collects data
// on a per-|latch_offset| basis.
pub struct LatencyStatsCollector<'a> {
    low_latency_scheduler: &'a LowLatencyScheduler,
    frame_stats_collector: FrameStatsCollector<'a>,
    half_frame_time: Cell<zx::Duration>,
    previous_frames: RefCell<VecDeque<Frame>>,
    latency_stats: RefCell<HashMap<zx::Duration, FrameCount>>,
}

impl LatencyStatsCollector<'_> {
    pub fn new(scheduler: &LowLatencyScheduler) -> LatencyStatsCollector<'_> {
        LatencyStatsCollector {
            low_latency_scheduler: scheduler,
            frame_stats_collector: FrameStatsCollector::new(scheduler),
            half_frame_time: Cell::new(zx::Duration::from_millis(4)),
            previous_frames: RefCell::new(VecDeque::new()),
            latency_stats: RefCell::new(HashMap::new()),
        }
    }

    // Prints the stats from FrameStatsCollector, plus success rate for each latch offset used (by
    // millisecond), from largest to smallest.
    // Example output:
    //
    // Frame Scheduling stats:
    //     Total produced frames: 1237
    //     Total presented: 1180 (95.39%)
    //     Total dropped: 57 (4.61%)
    //     Total delayed: 513 (41.47%)
    //     Total missed latch points: 568 (45.92%)
    //     Average latency: 45.69 ms
    // Latch offset 32 ms. Success Rate 97.56% Total: 41 Missed: 1
    // Latch offset 31 ms. Success Rate 100.00% Total: 41 Missed: 0
    // Latch offset 30 ms. Success Rate 100.00% Total: 46 Missed: 0
    // Latch offset 29 ms. Success Rate 96.88% Total: 32 Missed: 1
    // Latch offset 28 ms. Success Rate 100.00% Total: 27 Missed: 0
    // Latch offset 27 ms. Success Rate 100.00% Total: 48 Missed: 0
    // Latch offset 26 ms. Success Rate 100.00% Total: 37 Missed: 0
    // Latch offset 25 ms. Success Rate 100.00% Total: 43 Missed: 0
    // Latch offset 24 ms. Success Rate 100.00% Total: 40 Missed: 0
    // Latch offset 23 ms. Success Rate 100.00% Total: 50 Missed: 0
    // Latch offset 22 ms. Success Rate 100.00% Total: 48 Missed: 0
    // Latch offset 21 ms. Success Rate 100.00% Total: 42 Missed: 0
    // Latch offset 20 ms. Success Rate 100.00% Total: 43 Missed: 0
    // Latch offset 19 ms. Success Rate 100.00% Total: 37 Missed: 0
    // Latch offset 18 ms. Success Rate 100.00% Total: 41 Missed: 0
    // Latch offset 17 ms. Success Rate 100.00% Total: 24 Missed: 0
    // Latch offset 16 ms. Success Rate 100.00% Total: 30 Missed: 0
    // Latch offset 15 ms. Success Rate 2.08% Total: 48 Missed: 47
    // Latch offset 14 ms. Success Rate 0.00% Total: 35 Missed: 35
    // Latch offset 13 ms. Success Rate 0.00% Total: 34 Missed: 34
    // Latch offset 12 ms. Success Rate 0.00% Total: 39 Missed: 39
    // Latch offset 11 ms. Success Rate 0.00% Total: 30 Missed: 30
    // Latch offset 10 ms. Success Rate 0.00% Total: 45 Missed: 45
    // Latch offset 9 ms. Success Rate 0.00% Total: 34 Missed: 34
    // Latch offset 8 ms. Success Rate 0.00% Total: 44 Missed: 44
    // Latch offset 7 ms. Success Rate 0.00% Total: 33 Missed: 33
    // Latch offset 6 ms. Success Rate 0.00% Total: 37 Missed: 37
    // Latch offset 5 ms. Success Rate 0.00% Total: 31 Missed: 31
    // Latch offset 4 ms. Success Rate 0.00% Total: 45 Missed: 45
    // Latch offset 3 ms. Success Rate 0.00% Total: 30 Missed: 30
    // Latch offset 2 ms. Success Rate 0.00% Total: 39 Missed: 39
    // Latch offset 1 ms. Success Rate 0.00% Total: 43 Missed: 43

    pub fn stats_to_string(&self) -> String {
        let mut string_vec = Vec::new();
        string_vec.push(self.frame_stats_collector.stats_to_string());

        let mut entries: Vec<(zx::Duration, FrameCount)> = self
            .latency_stats
            .borrow()
            .iter()
            .map(|(key, value)| (key.clone(), value.clone()))
            .collect::<Vec<(zx::Duration, FrameCount)>>();
        entries.sort_by(|(key1, _), (key2, _)| key2.cmp(key1));
        for (key, value) in entries.iter() {
            string_vec.push(format!(
                "Latch offset {} ms. Success Rate {:.2}% Total: {} Missed: {}",
                key.into_millis(),
                100.0 * (1.0 - (value.missed_latch_points as f64) / (value.total as f64)),
                value.total,
                value.missed_latch_points
            ));
        }

        string_vec.join("\n")
    }
}

#[async_trait(?Send)]
impl SchedulingLib for LatencyStatsCollector<'_> {
    fn request_present(&self) {
        self.frame_stats_collector.request_present()
    }

    // on_frame_presented() tells us how the previous set of presents worked out, so here we can
    // track how successful our frame scheduling is.
    fn on_frame_presented(
        &self,
        actual_presentation_time: zx::Time,
        presented_infos: Vec<PresentedInfo>,
    ) {
        // Update per-present stats for all squashed and shown presents from the current frame.
        let previous_frames = &mut self.previous_frames.borrow_mut();
        let latency_stats = &mut self.latency_stats.borrow_mut();
        for PresentedInfo { actual_latch_point, .. } in presented_infos.iter() {
            let frame = previous_frames.pop_front().unwrap();
            let mut count = latency_stats
                .entry(frame.latch_offset)
                .or_insert(FrameCount { total: 0, missed_latch_points: 0 });
            count.total += 1;
            if actual_latch_point > &(frame.expected_latch_point + self.half_frame_time.get()) {
                count.missed_latch_points += 1;
            }
        }

        self.frame_stats_collector.on_frame_presented(actual_presentation_time, presented_infos)
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

        self.frame_stats_collector
            .on_next_frame_begin(additional_present_credits, future_presentation_infos)
    }

    // Tracks scheduling stats.
    async fn wait_to_update(&self) -> PresentParameters {
        let latch_offset = self.low_latency_scheduler.latch_offset.get();
        let present_parameters = self.frame_stats_collector.wait_to_update().await;

        // Gather timestamp data before returning.
        self.previous_frames.borrow_mut().push_back(Frame {
            latch_offset: latch_offset,
            expected_latch_point: present_parameters.expected_latch_point,
        });

        present_parameters
    }
}
