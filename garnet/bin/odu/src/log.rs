// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Thread level stats of the ongoing perf measurement. Each thread may own it's
//! own stat. Some stats are additive some are not. Aggregating them is slightly
//! tricky and may come at cost of precision.
//! latency is in microseconds
//! bandwidth is in MB (10^6 bytes) per second
//! IOPS is in number of IOs per second - duh

use {
    crate::io_packet::TimeInterval,
    crate::operations::{OperationType, PipelineStages},
    log::{
        debug, log_enabled, warn, Level, Level::Debug, LevelFilter, Metadata, Record,
        SetLoggerError,
    },
    std::time::Instant,
};

/// OperationStats maintains stats for an operation. In addition to maintaining total
/// time spent in each stage of io packet's pipeline, it also maintains total
/// number of IOs completed and total number of bytes transferred.
#[derive(Copy, Clone, Default)]
struct OperationStats {
    /// Sum of time all IoPacket spent actively in each stage.
    /// TODO: Create a type instead of using an array.
    stage_time_elapsed: [u128; PipelineStages::stage_count()],

    /// Total number of bytes transferred. The interpretation of this variable
    /// may depend on the type of operation.
    bytes_transferred: u64,

    /// Total IoPackets processed so far.
    io_count: u64,
}

impl OperationStats {
    pub fn log(
        &mut self,
        io_size: u64,
        stage_time_elapsed: &[TimeInterval; PipelineStages::stage_count()],
    ) {
        for stage in PipelineStages::iterator() {
            self.stage_time_elapsed[stage.stage_number()] +=
                stage_time_elapsed[stage.stage_number()].duration();
        }
        self.io_count += 1;
        self.bytes_transferred += io_size;
    }

    pub fn aggregate_summary(&mut self, stat1: &OperationStats) {
        // bytes transferred is additive.
        self.bytes_transferred += stat1.bytes_transferred;

        // io_count is additive.
        self.io_count += stat1.io_count;

        // This is debatable whether to consider adding or taking the
        // smallest/largest run. For start/end we chose longest run and for
        // stage duration we choose some of all time spent to get a different
        // perspective about the performance.
        for stage in PipelineStages::iterator() {
            self.stage_time_elapsed[stage.stage_number()] +=
                stat1.stage_time_elapsed[stage.stage_number()];
        }
    }

    /// Returns latency, bandwidth and IO per second for a given stage
    fn stage_stats(&self, stage: PipelineStages) -> (f64, f64, f64) {
        let mut duration_sec =
            self.stage_time_elapsed[stage.stage_number()] as f64 / 1_000_000_000 as f64;
        let duration_ns = self.stage_time_elapsed[stage.stage_number()];

        if duration_sec == 0.0 {
            warn!("Stage duration is zero for {:?}", stage);
            duration_sec = 1.0;
        }
        let latency_us = (duration_ns as f64 / 1_000 as f64) / self.io_count as f64;
        let bandwidth = (self.bytes_transferred / 1_000_000) as f64 / (duration_sec as f64);
        let iops_per_sec = self.io_count as f64 / (duration_sec as f64);
        (latency_us, bandwidth, iops_per_sec)
    }

    fn log_summary_per_stage(&self) {
        for stage in PipelineStages::iterator() {
            let threads_duration = self.stage_time_elapsed[stage.stage_number()];
            let (latency_us, bandwidth, iops_per_sec) = self.stage_stats(*stage);
            debug!(
                "{:?} TimeInStage:{} IOPS:{:.2} LAT(us):{:.2} BW(MB/s):{:.2}",
                *stage, threads_duration, iops_per_sec, latency_us, bandwidth
            );
        }
    }
}

/// Maintains vital stats for each operation type and also maintains time
/// duration this stat was active - time spent between calls to  start() and
/// end().
pub struct Stats {
    /// When was this Stats instance started. This is a snapshot of
    /// monotonically increasing system clock.
    start: Instant,

    /// When was the Stats instance done gathering stats.
    end: Instant,

    // TODO: Create a type instead of using an array.
    operations: [OperationStats; OperationType::operations_count()],
}

impl Stats {
    pub fn new() -> Stats {
        let now = Instant::now();
        Stats { start: now, operations: Default::default(), end: now }
    }

    pub fn log(
        &mut self,
        operation: OperationType,
        io_size: u64,
        stage_time_elapsed: &[TimeInterval; PipelineStages::stage_count()],
    ) {
        self.operations[operation.operation_number()].log(io_size, stage_time_elapsed);
    }

    pub fn start_clock(&mut self) {
        self.start = Instant::now();
    }

    pub fn stop_clock(&mut self) {
        self.end = Instant::now();
    }

    /// Aggregate stats from multiple stats into one. We consider longest running
    /// thread for duration of the run.
    pub fn aggregate_summary(&mut self, stat1: &Stats) {
        // We consider longest running thread.
        if self.end.duration_since(self.start) < stat1.end.duration_since(stat1.start) {
            self.start = stat1.start;
            self.end = stat1.end;
        }

        for operation in OperationType::iterator() {
            let self_ops = &mut self.operations[operation.operation_number()];
            let stat1_ops = &stat1.operations[operation.operation_number()];

            self_ops.aggregate_summary(stat1_ops);
        }
    }

    pub fn display_summary(&self) {
        let mut duration =
            self.end.duration_since(self.start).as_nanos() as f64 / 1_000_000_000 as f64;
        if duration == 0.0 {
            warn!("Stage duration is zero");
            duration = 1.0;
        }

        for operation in OperationType::iterator() {
            let stat = &self.operations[operation.operation_number()];

            if log_enabled!(Debug) {
                stat.log_summary_per_stage();
            }

            if stat.io_count > 0 {
                let (latency_us, bandwidth, iops_per_sec) = stat.stage_stats(PipelineStages::Issue);

                println!(
                    "Operation: {:?}\n\
                     \tRuntime:{:0.2}s IOs:{} bytes_transferred(MB):{:.2}\n\
                     \tIOPS:{:.2} LAT(us):{:.2} BW(MB/s):{:.2}",
                    operation,
                    duration,
                    stat.io_count,
                    stat.bytes_transferred as f64 / 1_000_000 as f64,
                    iops_per_sec,
                    latency_us,
                    bandwidth
                );
            }
        }
    }
}

struct SimpleLogger;

impl log::Log for SimpleLogger {
    fn enabled(&self, metadata: &Metadata<'_>) -> bool {
        metadata.level() <= Level::Debug
    }

    fn log(&self, record: &Record<'_>) {
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

#[cfg(test)]
mod tests {
    use {
        crate::io_packet::TimeInterval,
        crate::log::Stats,
        crate::operations::{OperationType, PipelineStages},
        std::cmp,
        std::{thread, time::Duration},
    };

    #[test]
    fn stats_test_new_initialized() {
        let stats = Stats::new();
        assert_eq!(stats.end.duration_since(stats.start).as_nanos(), 0);
        for operation in OperationType::iterator() {
            let operations = &stats.operations[operation.operation_number()];
            assert_eq!(operations.bytes_transferred, 0);
            assert_eq!(operations.io_count, 0);
            for stage in PipelineStages::iterator() {
                assert_eq!(operations.stage_time_elapsed[stage.stage_number()], 0);
            }
        }
    }

    fn log_update_helper(
        stats: &mut Stats,
        operation_sleep_time: Duration,
        stage_sleep_time: Duration,
    ) {
        for operation in OperationType::iterator() {
            let mut stage_time_elapsed = [TimeInterval::new(); PipelineStages::stage_count()];
            for stage in PipelineStages::iterator() {
                stage_time_elapsed[stage.stage_number()].start();
            }

            // sleep for few milliseconds before we end pipelines
            thread::sleep(
                operation_sleep_time
                    + Duration::from_millis(1 * operation.operation_number() as u64),
            );

            for stage in PipelineStages::iterator() {
                thread::sleep(stage_sleep_time);
                stage_time_elapsed[stage.stage_number()].end();
            }

            stats.log(*operation, operation.operation_number() as u64, &stage_time_elapsed);
        }
    }

    fn log_verify_helper(
        stats: &Stats,
        io_count: u64,
        operation_sleep_time: Duration,
        stage_sleep_time: Duration,
    ) {
        for operation in OperationType::iterator() {
            let op_stats = &stats.operations[operation.operation_number()];

            assert_eq!(op_stats.io_count, io_count);
            assert_eq!(op_stats.bytes_transferred, io_count * operation.operation_number() as u64);

            let operation_duration_ns = operation_sleep_time.as_nanos()
                + Duration::from_millis(1 * operation.operation_number() as u64).as_nanos();

            let mut stage_duration_ns: u128 = 0;
            for stage in PipelineStages::iterator() {
                stage_duration_ns += stage_sleep_time.as_nanos();

                assert!(
                    op_stats.stage_time_elapsed[stage.stage_number()]
                        > io_count as u128 * (operation_duration_ns + stage_duration_ns)
                );
            }
        }
        let mut total_duration: Duration = Duration::new(0, 0);
        for _ in 0..io_count {
            total_duration += operation_sleep_time * OperationType::operations_count() as u32;
        }
        assert!(stats.end.duration_since(stats.start) > total_duration);
    }

    #[test]
    fn stats_test_log_log_once() {
        let mut stats = Stats::new();
        stats.start_clock();
        log_update_helper(&mut stats, Duration::from_millis(10), Duration::from_millis(1));
        stats.stop_clock();
        log_verify_helper(&mut stats, 1, Duration::from_millis(10), Duration::from_millis(1));
    }

    #[test]
    fn stats_test_log_log_multiple() {
        let mut stats = Stats::new();
        stats.start_clock();
        let io_count = 4;
        for _ in 0..io_count {
            log_update_helper(&mut stats, Duration::from_millis(10), Duration::from_millis(1));
        }

        stats.stop_clock();
        log_verify_helper(
            &mut stats,
            io_count,
            Duration::from_millis(10),
            Duration::from_millis(1),
        );
    }

    #[test]
    fn stats_test_aggregate() {
        let mut stats1 = Stats::new();
        let mut stats2 = Stats::new();
        let io_count1 = 4;
        let io_count2 = 5;

        stats1.start_clock();
        for _ in 0..io_count1 {
            log_update_helper(&mut stats1, Duration::from_millis(10), Duration::from_millis(1));
        }
        stats1.stop_clock();

        stats2.start_clock();
        for _ in 0..io_count2 {
            log_update_helper(&mut stats2, Duration::from_millis(10), Duration::from_millis(1));
        }
        stats2.stop_clock();

        let mut aggregate = Stats::new();
        aggregate.aggregate_summary(&stats1);
        aggregate.aggregate_summary(&stats2);

        for operation in OperationType::iterator() {
            let aggregate_op = &aggregate.operations[operation.operation_number()];
            let op1 = &stats1.operations[operation.operation_number()];
            let op2 = &stats2.operations[operation.operation_number()];

            // io_counts should be cumulative.
            assert_eq!(aggregate_op.io_count, op1.io_count + op2.io_count);

            // bytes transferred should be cumulative.
            assert_eq!(
                aggregate_op.bytes_transferred,
                op1.bytes_transferred + op2.bytes_transferred
            );

            // Time spent in each stage is also cumulative.
            for stage in PipelineStages::iterator() {
                assert_eq!(
                    aggregate_op.stage_time_elapsed[stage.stage_number()],
                    op1.stage_time_elapsed[stage.stage_number()]
                        + op2.stage_time_elapsed[stage.stage_number()]
                );
            }
        }

        // For duration of the run, longest running instance should be considered.
        assert_eq!(
            aggregate.end.duration_since(aggregate.start).as_nanos(),
            cmp::max(
                stats1.end.duration_since(stats1.start).as_nanos(),
                stats2.end.duration_since(stats2.start).as_nanos()
            )
        );
    }

}
