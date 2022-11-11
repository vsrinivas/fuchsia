// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod block_device;
pub mod filesystem;
pub mod io_benchmarks;
#[macro_use]
mod trace;

use {
    async_trait::async_trait,
    regex::RegexSet,
    serde::Serialize,
    serde_json,
    std::{io::Write, sync::Arc, time::Instant, vec::Vec},
    tracing::info,
};

pub use crate::{
    block_device::{BlockDevice, BlockDeviceConfig, BlockDeviceFactory},
    filesystem::{Filesystem, FilesystemConfig},
};

/// How long a benchmarked operation took to complete.
#[derive(Debug)]
pub struct OperationDuration(f64);

/// A timer for tracking how long an operation took to complete.
#[derive(Debug)]
pub struct OperationTimer {
    start_time: Instant,
}

impl OperationTimer {
    pub fn start() -> Self {
        let start_time = Instant::now();
        Self { start_time }
    }

    pub fn stop(self) -> OperationDuration {
        OperationDuration(Instant::now().duration_since(self.start_time).as_nanos() as f64)
    }
}

/// A trait representing a single benchmark.
#[async_trait]
pub trait Benchmark {
    async fn run(&self, fs: &mut dyn Filesystem) -> Vec<OperationDuration>;
    fn name(&self) -> String;
}

struct BenchmarkResultsStatistics {
    p50: f64,
    p95: f64,
    p99: f64,
    mean: f64,
    min: f64,
    max: f64,
    count: u64,
}

struct BenchmarkResults {
    benchmark_name: String,
    filesystem_name: String,
    values: Vec<OperationDuration>,
}

fn percentile(data: &[f64], p: u8) -> f64 {
    assert!(p <= 100 && !data.is_empty());
    if data.len() == 1 || p == 0 {
        return *data.first().unwrap();
    }
    if p == 100 {
        return *data.last().unwrap();
    }
    let rank = ((p as f64) / 100.0) * ((data.len() - 1) as f64);
    // The rank is unlikely to be a whole number. Use a linear interpolation between the 2 closest
    // data points.
    let fraction = rank - rank.floor();
    let rank = rank as usize;
    data[rank] + (data[rank + 1] - data[rank]) * fraction
}

impl BenchmarkResults {
    fn statistics(&self) -> BenchmarkResultsStatistics {
        let mut data: Vec<f64> = self.values.iter().map(|d| d.0).collect();
        data.sort_by(|a, b| a.partial_cmp(b).unwrap());
        let min = *data.first().unwrap();
        let max = *data.last().unwrap();
        let p50 = percentile(&data, 50);
        let p95 = percentile(&data, 95);
        let p99 = percentile(&data, 99);

        let mut sum = 0.0;
        for value in &data {
            sum += value;
        }
        let count = data.len() as u64;
        let mean = sum / (count as f64);
        BenchmarkResultsStatistics { min, p50, p95, p99, max, mean, count }
    }

    fn pretty_row_header() -> prettytable::Row {
        use prettytable::{cell, row};
        row![
            l->"FS",
            l->"Benchmark",
            r->"Min",
            r->"50th Percentile",
            r->"95th Percentile",
            r->"99th Percentile",
            r->"Max",
            r->"Mean",
            r->"Count"
        ]
    }

    fn pretty_row(&self) -> prettytable::Row {
        let stats = self.statistics();
        use prettytable::{cell, row};
        row![
            l->self.filesystem_name,
            l->self.benchmark_name,
            r->format_nanos_with_commas(stats.min),
            r->format_nanos_with_commas(stats.p50),
            r->format_nanos_with_commas(stats.p95),
            r->format_nanos_with_commas(stats.p99),
            r->format_nanos_with_commas(stats.max),
            r->format_nanos_with_commas(stats.mean),
            r->format_u64_with_commas(stats.count),
        ]
    }

    fn csv_row_header() -> String {
        "FS,Benchmark,Min,50th Percentile,95th Percentile,99th Percentile,Max,Mean,Count\n"
            .to_owned()
    }

    fn csv_row(&self) -> String {
        let stats = self.statistics();
        format!(
            "{},{},{},{},{},{},{},{},{}\n",
            self.filesystem_name,
            self.benchmark_name,
            stats.min,
            stats.p50,
            stats.p95,
            stats.p99,
            stats.max,
            stats.mean,
            stats.count
        )
    }
}

struct BenchmarkConfig {
    benchmark: Box<dyn Benchmark>,
    filesystem_config: Arc<dyn FilesystemConfig>,
}

impl BenchmarkConfig {
    async fn run(&self, block_device_factory: &impl BlockDeviceFactory) -> BenchmarkResults {
        let mut fs = self.filesystem_config.start_filesystem(block_device_factory).await;
        info!("Running {}", self.name());
        let timer = OperationTimer::start();
        let durations = self.benchmark.run(fs.as_mut()).await;
        let benchmark_duration = timer.stop();
        info!("Finished {} {}ns", self.name(), format_nanos_with_commas(benchmark_duration.0));
        fs.shutdown().await;

        BenchmarkResults {
            benchmark_name: self.benchmark.name(),
            filesystem_name: self.filesystem_config.name(),
            values: durations,
        }
    }

    fn name(&self) -> String {
        format!("{}/{}", self.benchmark.name(), self.filesystem_config.name())
    }

    fn matches(&self, filter: &RegexSet) -> bool {
        if filter.is_empty() {
            return true;
        }
        filter.is_match(&self.name())
    }
}

/// A collection of benchmarks and the filesystems to run the benchmarks against.
pub struct BenchmarkSet {
    benchmarks: Vec<BenchmarkConfig>,
}

impl BenchmarkSet {
    pub fn new() -> Self {
        Self { benchmarks: Vec::new() }
    }

    /// Adds a new benchmark with the filesystems that the benchmark should be run against to the
    /// `BenchmarkSet`.
    pub fn add_benchmark<
        'a,
        B: Benchmark + Clone + 'static,
        Iter: IntoIterator<Item = &'a Arc<dyn FilesystemConfig>>,
    >(
        &mut self,
        benchmark: B,
        filesystem_configs: Iter,
    ) {
        for filesystem_config in filesystem_configs {
            self.benchmarks.push(BenchmarkConfig {
                benchmark: Box::new(benchmark.clone()),
                filesystem_config: filesystem_config.clone(),
            });
        }
    }

    /// Runs all of the added benchmarks against their configured filesystems. The filesystems will
    /// be brought up on block devices created from `block_device_factory`.
    pub async fn run<BDF: BlockDeviceFactory>(
        &self,
        block_device_factory: &BDF,
        filter: &RegexSet,
    ) -> BenchmarkSetResults {
        let mut results = Vec::new();
        for benchmark in &self.benchmarks {
            if benchmark.matches(filter) {
                results.push(benchmark.run(block_device_factory).await);
            }
        }
        BenchmarkSetResults { results }
    }
}

/// All of the benchmark results from `BenchmarkSet::run`.
pub struct BenchmarkSetResults {
    results: Vec<BenchmarkResults>,
}

impl BenchmarkSetResults {
    /// Writes the benchmark results in the fuchsiaperf.json format to `writer`.
    pub fn write_fuchsia_perf_json<F: Write>(&self, writer: F) {
        const NANOSECONDS: &str = "nanoseconds";
        const TEST_SUITE: &str = "fuchsia.storage";
        let results: Vec<FuchsiaPerfBenchmarkResult> = self
            .results
            .iter()
            .map(|result| FuchsiaPerfBenchmarkResult {
                label: format!("{}/{}", result.benchmark_name, result.filesystem_name),
                test_suite: TEST_SUITE.to_owned(),
                unit: NANOSECONDS.to_owned(),
                values: result.values.iter().map(|d| d.0).collect(),
            })
            .collect();

        serde_json::to_writer_pretty(writer, &results).unwrap()
    }

    /// Writes a summary of the benchmark results as a human readable table to `writer`.
    pub fn write_table<F: Write>(&self, mut writer: F) {
        let mut table = prettytable::Table::new();
        table.set_format(*prettytable::format::consts::FORMAT_CLEAN);
        table.add_row(BenchmarkResults::pretty_row_header());
        for result in &self.results {
            table.add_row(result.pretty_row());
        }
        table.print(&mut writer).unwrap();
    }

    /// Writes a summary of the benchmark results in csv format to `writer`.
    pub fn write_csv<F: Write>(&self, mut writer: F) {
        writer.write_all(BenchmarkResults::csv_row_header().as_bytes()).unwrap();
        for result in &self.results {
            writer.write_all(result.csv_row().as_bytes()).unwrap();
        }
    }
}

/// A struct that can be json serialized to the fuchsiaperf.json format.
#[derive(Serialize)]
struct FuchsiaPerfBenchmarkResult {
    label: String,
    test_suite: String,
    unit: String,
    values: Vec<f64>,
}

fn format_u64_with_commas(num: u64) -> String {
    let mut result = String::new();
    let num = num.to_string();
    let digit_count = num.len();
    for (i, d) in num.char_indices() {
        result.push(d);
        let rpos = digit_count - i;
        if rpos != 1 && rpos % 3 == 1 {
            result.push(',');
        }
    }
    result
}

fn format_nanos_with_commas(nanos: f64) -> String {
    assert!(nanos > 0.0);
    format_u64_with_commas(nanos as u64)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            block_device::PanickingBlockDeviceFactory, filesystem::MountedFilesystem,
            FilesystemConfig,
        },
    };

    fn assert_approximately_eq(a: f64, b: f64) {
        assert!((a - b).abs() < f64::EPSILON, "{} != {}", a, b);
    }

    #[derive(Clone)]
    struct TestBenchmark {
        name: &'static str,
    }

    #[async_trait]
    impl Benchmark for TestBenchmark {
        async fn run(&self, _fs: &mut dyn Filesystem) -> Vec<OperationDuration> {
            vec![OperationDuration(1.0), OperationDuration(2.0), OperationDuration(3.0)]
        }

        fn name(&self) -> String {
            self.name.to_owned()
        }
    }

    struct TestFilesystem {
        name: String,
    }

    impl TestFilesystem {
        fn new(name: &str) -> Arc<Self> {
            Arc::new(Self { name: name.to_owned() })
        }
    }

    #[async_trait]
    impl FilesystemConfig for TestFilesystem {
        async fn start_filesystem(
            &self,
            _block_device_factory: &dyn BlockDeviceFactory,
        ) -> Box<dyn Filesystem> {
            Box::new(TestFilesystemInstance {})
        }

        fn name(&self) -> String {
            self.name.clone()
        }
    }

    struct TestFilesystemInstance {}

    #[async_trait]
    impl Filesystem for TestFilesystemInstance {
        async fn clear_cache(&mut self) {}

        async fn shutdown(self: Box<Self>) {}

        fn benchmark_dir(&self) -> &std::path::Path {
            panic!("not supported");
        }
    }

    #[fuchsia::test]
    async fn run_benchmark_set() {
        let mut benchmark_set = BenchmarkSet::new();

        let mut filesystems: Vec<Arc<dyn FilesystemConfig>> = vec![TestFilesystem::new("fs1")];
        benchmark_set.add_benchmark(TestBenchmark { name: "test1" }, &filesystems);

        filesystems.push(TestFilesystem::new("fs2"));
        benchmark_set.add_benchmark(TestBenchmark { name: "test2" }, &filesystems);

        let block_device_factory = PanickingBlockDeviceFactory::new();
        let results = benchmark_set.run(&block_device_factory, &RegexSet::empty()).await;
        let results = results.results;
        assert_eq!(results.len(), 3);

        assert_eq!(results[0].benchmark_name, "test1");
        assert_eq!(results[0].filesystem_name, "fs1");
        assert_eq!(results[0].values.len(), 3);

        assert_eq!(results[1].benchmark_name, "test2");
        assert_eq!(results[1].filesystem_name, "fs1");
        assert_eq!(results[1].values.len(), 3);

        assert_eq!(results[2].benchmark_name, "test2");
        assert_eq!(results[2].filesystem_name, "fs2");
        assert_eq!(results[2].values.len(), 3);
    }

    #[fuchsia::test]
    fn benchmark_filters() {
        let config = BenchmarkConfig {
            benchmark: Box::new(TestBenchmark { name: "read_warm" }),
            filesystem_config: Arc::new(MountedFilesystem::new("filesystem")),
        };
        // Accepted patterns.
        assert!(config.matches(&RegexSet::empty()));
        assert!(config.matches(&RegexSet::new([r""]).unwrap()));
        assert!(config.matches(&RegexSet::new([r"filesystem"]).unwrap()));
        assert!(config.matches(&RegexSet::new([r"/filesystem"]).unwrap()));
        assert!(config.matches(&RegexSet::new([r"read_warm"]).unwrap()));
        assert!(config.matches(&RegexSet::new([r"read_warm/"]).unwrap()));
        assert!(config.matches(&RegexSet::new([r"read_warm/filesystem"]).unwrap()));
        assert!(config.matches(&RegexSet::new([r"warm"]).unwrap()));

        // Rejected patterns.
        assert!(!config.matches(&RegexSet::new([r"cold"]).unwrap()));
        assert!(!config.matches(&RegexSet::new([r"fxfs"]).unwrap()));
        assert!(!config.matches(&RegexSet::new([r"^filesystem"]).unwrap()));
        assert!(!config.matches(&RegexSet::new([r"read_warm$"]).unwrap()));

        // Matches "warm".
        assert!(config.matches(&RegexSet::new([r"warm", r"cold"]).unwrap()));
        // Matches both.
        assert!(config.matches(&RegexSet::new([r"warm", r"filesystem"]).unwrap()));
        // Matches neither.
        assert!(!config.matches(&RegexSet::new([r"cold", r"fxfs"]).unwrap()));
    }

    #[fuchsia::test]
    fn result_statistics() {
        let benchmark_name = "benchmark-name".to_string();
        let filesystem_name = "filesystem-name".to_string();
        let result = BenchmarkResults {
            benchmark_name: benchmark_name.clone(),
            filesystem_name: filesystem_name.clone(),
            values: vec![
                OperationDuration(6.0),
                OperationDuration(7.0),
                OperationDuration(5.0),
                OperationDuration(8.0),
                OperationDuration(4.0),
            ],
        };
        let stats = result.statistics();
        assert_approximately_eq(stats.p50, 6.0);
        assert_approximately_eq(stats.p95, 7.8);
        assert_approximately_eq(stats.p99, 7.96);
        assert_approximately_eq(stats.mean, 6.0);
        assert_eq!(stats.min, 4.0);
        assert_eq!(stats.max, 8.0);
        assert_eq!(stats.count, 5);
    }

    #[fuchsia::test]
    fn percentile_test() {
        let data = [4.0];
        assert_approximately_eq(percentile(&data, 50), 4.0);
        assert_approximately_eq(percentile(&data, 95), 4.0);
        assert_approximately_eq(percentile(&data, 99), 4.0);

        let data = [4.0, 5.0];
        assert_approximately_eq(percentile(&data, 50), 4.5);
        assert_approximately_eq(percentile(&data, 95), 4.95);
        assert_approximately_eq(percentile(&data, 99), 4.99);

        let data = [4.0, 5.0, 6.0];
        assert_approximately_eq(percentile(&data, 50), 5.0);
        assert_approximately_eq(percentile(&data, 95), 5.9);
        assert_approximately_eq(percentile(&data, 99), 5.98);

        let data = [4.0, 5.0, 6.0, 7.0];
        assert_approximately_eq(percentile(&data, 50), 5.5);
        assert_approximately_eq(percentile(&data, 95), 6.85);
        assert_approximately_eq(percentile(&data, 99), 6.97);

        let data = [4.0, 5.0, 6.0, 7.0, 8.0];
        assert_approximately_eq(percentile(&data, 50), 6.0);
        assert_approximately_eq(percentile(&data, 95), 7.8);
        assert_approximately_eq(percentile(&data, 99), 7.96);
    }

    #[fuchsia::test]
    fn format_u64_with_commas_test() {
        assert_eq!(format_u64_with_commas(0), "0");
        assert_eq!(format_u64_with_commas(1), "1");
        assert_eq!(format_u64_with_commas(99), "99");
        assert_eq!(format_u64_with_commas(999), "999");
        assert_eq!(format_u64_with_commas(1_000), "1,000");
        assert_eq!(format_u64_with_commas(999_999), "999,999");
        assert_eq!(format_u64_with_commas(1_000_000), "1,000,000");
        assert_eq!(format_u64_with_commas(999_999_999), "999,999,999");
        assert_eq!(format_u64_with_commas(1_000_000_000), "1,000,000,000");
        assert_eq!(format_u64_with_commas(999_999_999_999), "999,999,999,999");
    }

    #[fuchsia::test]
    fn format_nanos_with_commas_test() {
        assert_eq!(format_nanos_with_commas(0.1), "0");
        assert_eq!(format_nanos_with_commas(0.9), "0");
        assert_eq!(format_nanos_with_commas(1.0), "1");
        assert_eq!(format_nanos_with_commas(1.9), "1");
        assert_eq!(format_nanos_with_commas(999.9), "999");
        assert_eq!(format_nanos_with_commas(1_000.0), "1,000");
        assert_eq!(format_nanos_with_commas(999_999.9), "999,999");
        assert_eq!(format_nanos_with_commas(1_000_000.0), "1,000,000");
    }
}
