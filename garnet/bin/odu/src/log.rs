// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Thread level stats of the ongoing perf measurement. Each thread may own it's
//! own stat. Some stats are additive some are not. Aggregating them is slightly
//! tricky and may come at cost of precision.
//! latency is in microseconds
//! bandwidth is in MB (10^6 bytes) per second
//! IOPS is in number of IOs per second - duh

extern crate log;

use {
    crate::io_packet::IoPacketType,
    crate::operations::{OperationType, PipelineStages},
    log::{
        debug, error, log_enabled, warn, Level, Level::Debug, LevelFilter, Metadata, Record,
        SetLoggerError,
    },
    std::{process, time::Instant},
};

pub struct Stats {
    /// When was this Stats instance started. This is a snapshot of
    /// monotonically increasing system clock.
    start_instant: Instant,

    /// When was the Stats instance done gathering stats.
    end_instant: Instant,

    /// Sum of time all IoPacket spent actively in each stage.
    stage_duration: [u128; PipelineStages::stage_count()],

    /// Total IoPackets processed so far.
    io_count: u64,

    /// Total write bytes transferred. So far only write operations are
    /// supported.
    write_size: u64,
}

impl Stats {
    pub fn new() -> Stats {
        let now = Instant::now();
        Stats {
            start_instant: now,
            stage_duration: [0; PipelineStages::stage_count()],
            io_count: 0,
            write_size: 0,
            end_instant: now,
        }
    }

    pub fn log_stat(&mut self, io_packet: &IoPacketType) {
        for stage in PipelineStages::iterator() {
            self.stage_duration[stage.stage_number()] += io_packet.stage_duration(*stage);
        }
        self.io_count += 1;
        match io_packet.operation_type() {
            OperationType::Write => self.write_size += io_packet.io_size(),
            OperationType::Exit => {}
            _ => {
                error!("unsupported operation");
                process::abort();
            }
        }
    }

    pub fn start_clock(&mut self) {
        self.start_instant = Instant::now();
    }

    pub fn stop_clock(&mut self) {
        self.end_instant = Instant::now();
    }

    /// Aggregate stats from multiple stats into one. We consider longest running
    /// thread for duration of the run.
    pub fn aggregate_summary(&mut self, stat1: &Stats) {
        // We consider longest running thread.
        if self.start_instant > stat1.start_instant {
            self.start_instant = stat1.start_instant;
        }

        // We consider longest running thread.
        if self.end_instant < stat1.end_instant {
            self.end_instant = stat1.end_instant;
        }

        // bytes transferred is additive.
        self.write_size += stat1.write_size;

        // bytes transferred is additive.
        self.io_count += stat1.io_count;

        // This is debatable whether to consider adding or taking the
        // smallest/largest run. For start_instant/end_instant we chose longest run and for
        // stage duration we choose some of all time spent to get a different
        // perspective about the performance.
        for stage in PipelineStages::iterator() {
            self.stage_duration[stage.stage_number()] += stat1.stage_duration[stage.stage_number()];
        }
    }

    /// Returns latency, bandwidth and IO per second for a given stage
    fn stage_stats(&self, stage: PipelineStages) -> (f64, f64, f64) {
        let mut duration_sec =
            self.stage_duration[stage.stage_number()] as f64 / 1_000_000_000 as f64;
        let duration_ns = self.stage_duration[stage.stage_number()];

        if duration_sec == 0.0 {
            warn!("Stage duration is zero for {:?}", stage);
            duration_sec = 1.0;
        }
        let latency_us = (duration_ns as f64 / 1_000 as f64) / self.io_count as f64;
        let write_bandwidth = (self.write_size / 1_000_000) as f64 / (duration_sec as f64);
        let iops_per_sec = self.io_count as f64 / (duration_sec as f64);
        (latency_us, write_bandwidth, iops_per_sec)
    }

    fn log_summary_per_stage(&self) {
        for stage in PipelineStages::iterator() {
            let threads_duration = self.stage_duration[stage.stage_number()];
            let (latency_us, write_bandwidth, iops_per_sec) = self.stage_stats(*stage);
            debug!(
                "{:?} TimeInStage:{} IOPS:{:.2} LAT(us):{:.2} BW(MB/s):{:.2}",
                *stage, threads_duration, iops_per_sec, latency_us, write_bandwidth
            );
        }
    }

    pub fn display_summary(&self) {
        let mut duration = self.end_instant.duration_since(self.start_instant).as_nanos() as f64
            / 1_000_000_000 as f64;
        if duration == 0.0 {
            warn!("Stage duration is zero");
            duration = 1.0;
        }
        if log_enabled!(Debug) {
            self.log_summary_per_stage();
        }
        println!(
            "Runtime:{}s IOs:{} write_size(MB):{:.2}",
            duration,
            self.io_count,
            self.write_size as f64 / 1_000_000 as f64
        );
        let (latency_us, write_bandwidth, iops_per_sec) = self.stage_stats(PipelineStages::Issue);
        println!(
            "IOPS:{:.2} LAT(us):{:.2} BW(MB/s):{:.2}",
            iops_per_sec, latency_us, write_bandwidth
        );
    }
}

struct SimpleLogger;

impl log::Log for SimpleLogger {
    fn enabled(&self, metadata: &Metadata) -> bool {
        metadata.level() <= Level::Debug
    }

    fn log(&self, record: &Record) {
        if self.enabled(record.metadata()) {
            println!("{} - {}", record.level(), record.args());
        }
    }

    fn flush(&self) {}
}

static LOGGER: SimpleLogger = SimpleLogger;

pub fn log_init() -> Result<(), SetLoggerError> {
    log::set_logger(&LOGGER).map(|()| log::set_max_level(LevelFilter::Info))
}
