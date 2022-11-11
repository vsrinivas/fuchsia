// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    regex::{Regex, RegexSetBuilder},
    std::{path::PathBuf, sync::Arc, vec::Vec},
    storage_benchmarks::{
        block_device::PanickingBlockDeviceFactory,
        filesystem::MountedFilesystem,
        io_benchmarks::{
            ReadRandomWarm, ReadSequentialWarm, WriteRandomCold, WriteRandomWarm,
            WriteSequentialCold, WriteSequentialWarm,
        },
        BenchmarkSet, FilesystemConfig,
    },
};

/// Fuchsia Filesystem Benchmarks
#[derive(argh::FromArgs)]
struct Args {
    /// outputs a summary of the benchmark results in csv format.
    #[argh(switch)]
    output_csv: bool,

    /// regex to specify a subset of benchmarks to run. Multiple regex can be provided and
    /// benchmarks matching any of them will be run. The benchmark names are formatted as
    /// "<benchmark>/<filesystem>". All benchmarks are run if no filter is provided.
    #[argh(option)]
    filter: Vec<Regex>,

    /// directory to run the benchmarks out of.
    ///
    /// Warning: the contents of the directory may be deleted by the benchmarks.
    #[argh(option)]
    benchmark_dir: PathBuf,
}

fn build_benchmark_set(dir: PathBuf) -> BenchmarkSet {
    let filesystems: Vec<Arc<dyn FilesystemConfig>> = vec![Arc::new(MountedFilesystem::new(dir))];
    const OP_SIZE: usize = 8 * 1024;
    const OP_COUNT: usize = 1024;

    let mut benchmark_set = BenchmarkSet::new();
    benchmark_set.add_benchmark(ReadSequentialWarm::new(OP_SIZE, OP_COUNT), &filesystems);
    benchmark_set.add_benchmark(ReadRandomWarm::new(OP_SIZE, OP_COUNT), &filesystems);
    benchmark_set.add_benchmark(WriteSequentialWarm::new(OP_SIZE, OP_COUNT), &filesystems);
    benchmark_set.add_benchmark(WriteRandomWarm::new(OP_SIZE, OP_COUNT), &filesystems);
    benchmark_set.add_benchmark(WriteSequentialCold::new(OP_SIZE, OP_COUNT), &filesystems);
    benchmark_set.add_benchmark(WriteRandomCold::new(OP_SIZE, OP_COUNT), &filesystems);

    benchmark_set
}

#[fuchsia::main]
async fn main() {
    let args: Args = argh::from_env();

    std::fs::create_dir_all(&args.benchmark_dir).unwrap_or_else(|e| {
        panic!("Failed to create directory '{}': {:?}", args.benchmark_dir.display(), e)
    });

    let mut filter = RegexSetBuilder::new(args.filter.iter().map(|f| f.as_str()));
    filter.case_insensitive(true);
    let filter = filter.build().unwrap();

    let block_device_factory = PanickingBlockDeviceFactory::new();
    let benchmark_suite = build_benchmark_set(args.benchmark_dir);
    let results = benchmark_suite.run(&block_device_factory, &filter).await;

    results.write_table(std::io::stdout());
    if args.output_csv {
        results.write_csv(std::io::stdout())
    }
}
