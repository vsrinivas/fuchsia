// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        benchmarks::{
            ReadRandomCold, ReadRandomWarm, ReadSequentialCold, ReadSequentialWarm,
            WriteRandomCold, WriteRandomWarm, WriteSequentialCold, WriteSequentialWarm,
        },
        framework::{BenchmarkSet, FilesystemConfig, FvmVolumeFactory},
    },
    regex::Regex,
    std::fs::OpenOptions,
    std::path::PathBuf,
};

mod benchmarks;
mod framework;

/// Fuchsia Filesystem Benchmarks
#[derive(argh::FromArgs)]
struct Args {
    /// path to write the fuchsiaperf formatted benchmark results to.
    #[argh(option)]
    output_fuchsiaperf: Option<PathBuf>,

    /// outputs a summary of the benchmark results in csv format.
    #[argh(switch)]
    output_csv: bool,

    /// regex to specify a subset of benchmarks to run. Multiple regex can be provided and
    /// benchmarks matching any of them will be run. The benchmark names are formatted as
    /// "<benchmark>/<filesystem>". All benchmarks are run if no filter is provided.
    #[argh(option)]
    filter: Vec<Regex>,

    /// registers a trace provider and adds a trace duration with the benchmarks name around each
    /// benchmark.
    #[argh(switch)]
    enable_tracing: bool,
}

fn build_benchmark_set() -> BenchmarkSet {
    let mutable_filesystems = [
        FilesystemConfig::Fxfs,
        FilesystemConfig::F2fs,
        FilesystemConfig::Memfs,
        FilesystemConfig::Minfs,
    ];
    const OP_SIZE: usize = 8 * 1024;
    const OP_COUNT: usize = 1024;

    let mut benchmark_set = BenchmarkSet::new();
    benchmark_set.add_benchmark(ReadSequentialCold::new(OP_SIZE, OP_COUNT), &mutable_filesystems);
    benchmark_set.add_benchmark(ReadSequentialWarm::new(OP_SIZE, OP_COUNT), &mutable_filesystems);
    benchmark_set.add_benchmark(ReadRandomCold::new(OP_SIZE, OP_COUNT), &mutable_filesystems);
    benchmark_set.add_benchmark(ReadRandomWarm::new(OP_SIZE, OP_COUNT), &mutable_filesystems);
    benchmark_set.add_benchmark(WriteSequentialCold::new(OP_SIZE, OP_COUNT), &mutable_filesystems);
    benchmark_set.add_benchmark(WriteSequentialWarm::new(OP_SIZE, OP_COUNT), &mutable_filesystems);
    benchmark_set.add_benchmark(WriteRandomCold::new(OP_SIZE, OP_COUNT), &mutable_filesystems);
    benchmark_set.add_benchmark(WriteRandomWarm::new(OP_SIZE, OP_COUNT), &mutable_filesystems);

    benchmark_set
}

#[fuchsia::main(logging_tags = ["storage_benchmarks"])]
async fn main() {
    let args: Args = argh::from_env();
    if args.enable_tracing {
        fuchsia_trace_provider::trace_provider_create_with_fdio();
    }

    let fvm_volume_factory = FvmVolumeFactory::new().await;
    let benchmark_suite = build_benchmark_set();
    let results = benchmark_suite.run(&fvm_volume_factory, &args.filter).await;

    results.write_table(std::io::stdout());
    if args.output_csv {
        results.write_csv(std::io::stdout())
    }
    if let Some(path) = args.output_fuchsiaperf {
        let file = OpenOptions::new().write(true).create(true).open(path).unwrap();
        results.write_fuchsia_perf_json(file);
    }
}
