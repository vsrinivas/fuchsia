// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    argh::{FromArgValue, FromArgs},
    bench_utils::{generate_selectors_till_level, parse_selectors, InspectHierarchyGenerator},
    fuchsia_inspect::{
        hierarchy::filter_hierarchy, testing::DiagnosticsHierarchyGetter, Inspector,
    },
    fuchsia_trace as ftrace,
    fuchsia_trace_provider::trace_provider_create_with_fdio,
    lazy_static::lazy_static,
    paste,
    rand::{rngs::StdRng, SeedableRng},
};

mod bench_utils;

lazy_static! {
    static ref SELECTOR_TILL_LEVEL_30: Vec<String> = generate_selectors_till_level(30);
}

const HIERARCHY_GENERATOR_SEED: u64 = 0;

#[derive(FromArgs)]
#[argh(description = "Rust inspect benchmarks")]
struct Args {
    #[argh(option, description = "number of iterations", short = 'i', default = "500")]
    iterations: usize,

    #[argh(option, description = "which benchmark to run (writer, selector)")]
    benchmark: BenchmarkOption,
}

enum BenchmarkOption {
    Selector,
}

impl FromArgValue for BenchmarkOption {
    fn from_arg_value(value: &str) -> Result<Self, String> {
        match value {
            "selector" => Ok(BenchmarkOption::Selector),
            _ => Err(format!("Unknown benchmark \"{}\"", value)),
        }
    }
}

// Generate a function to benchmark inspect hierarchy filtering.
// The benchmark takes a snapshot of a seedable randomly generated
// inspect hierarchy in a vmo and then applies the given selectors
// to the snapshot to filter it down.
macro_rules! generate_selector_benchmark_fn {
    ($name: expr, $size: expr, $label:expr, $selectors: expr) => {
        paste::paste! {
            fn [<$name _ $size>](iterations: usize) {
                let inspector = Inspector::new();
                let mut hierarchy_generator = InspectHierarchyGenerator::new(
                    StdRng::seed_from_u64(HIERARCHY_GENERATOR_SEED), inspector);
                hierarchy_generator.generate_hierarchy($size);
                let hierarchy_matcher = parse_selectors(&$selectors);

                for _ in 0..iterations {
                // Trace format is <label>/<size>
                ftrace::duration!(
                    "benchmark",
                    concat!(
                        $label,
                        "/",
                        stringify!($size),
                    )
                );

                let hierarchy = hierarchy_generator.get_diagnostics_hierarchy().into_owned();
                let _ = filter_hierarchy(hierarchy, &hierarchy_matcher)
                    .expect("Unable to filter hierarchy.");
                }

            }
        }
    };
}

macro_rules! generate_selector_benchmarks {
    ($name: expr, $label: expr, $selectors: expr, [$($size: expr),*]) => {
        $(
            generate_selector_benchmark_fn!(
                $name,
                $size,
                $label,
                $selectors
            );

        )*
    };
}

generate_selector_benchmarks!(
    bench_snapshot_and_select,
    "SnapshotAndSelect",
    SELECTOR_TILL_LEVEL_30,
    [10, 100, 1000, 10000, 100000]
);

fn selector_benchmark(iterations: usize) {
    bench_snapshot_and_select_10(iterations);
    bench_snapshot_and_select_100(iterations);
    bench_snapshot_and_select_1000(iterations);
    bench_snapshot_and_select_10000(iterations);
    bench_snapshot_and_select_100000(iterations);
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    trace_provider_create_with_fdio();
    let args: Args = argh::from_env();

    match args.benchmark {
        BenchmarkOption::Selector => {
            selector_benchmark(args.iterations);
        }
    }

    Ok(())
}
