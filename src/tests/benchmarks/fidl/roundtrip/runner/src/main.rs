// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_async as fasync, fuchsia_component::client::launcher, fuchsia_syslog as flog,
    fuchsia_syslog::fx_log_info, itertools::iproduct, serde_json::json, std::fs::File,
    std::io::Write, std::ops::Deref, std::sync::Arc, std::time::Duration,
};

mod benchmark;
mod binding;
mod global_executor;
use benchmark::*;
use binding::*;
use std::env;

mod benchmarks;

const WARM_UP_RUNS: usize = 5;
const MEASURE_RUNS: usize = 200;

#[derive(Clone)]
struct Benchmark {
    pub benchmark: Arc<BenchmarkFn>,
    pub binding: LaunchedBinding,
    pub size: usize,
    pub concurrency: usize,
}

impl Benchmark {
    pub fn product(
        benchmarks: Vec<BenchmarkFn>,
        bindings: Vec<LaunchedBinding>,
        concurrencies: Vec<usize>,
    ) -> Vec<Benchmark> {
        itertools::iproduct!(
            benchmarks.into_iter().map(|b| Arc::new(b)),
            bindings.into_iter(),
            concurrencies.into_iter()
        )
        .map(|(benchmark, binding, concurrency)| {
            benchmark.sizes().into_iter().map(move |size| Benchmark {
                benchmark: benchmark.clone(),
                binding: binding.clone(),
                size,
                concurrency,
            })
        })
        .flatten()
        .collect()
    }
    pub fn name(&self) -> String {
        format!(
            "{}/{}/{}/{}/{}",
            self.binding.name,
            "Roundtrip".to_string(),
            self.benchmark.name(),
            self.benchmark.format_size(self.size),
            format!("Concurrency{}", self.concurrency)
        )
    }
    pub fn run(&self) -> Duration {
        self.benchmark.run(self.binding.proxy.deref().clone(), self.size, self.concurrency)
    }
}

struct BenchmarkResult {
    pub label: String,
    pub test_suite: String,
    pub values: Vec<Duration>,
}

impl BenchmarkResult {
    fn json(&self) -> serde_json::Value {
        json!({
            "label": self.label,
            "test_suite": self.test_suite,
            "unit": "ns",
            "values": self.values.iter().map(|d| d.as_nanos() as f64).collect::<Vec<f64>>(),
            "split_first": false,
        })
    }
}

fn run_benchmarks(
    binding_configs: Vec<BindingConfig>,
    benchmarks: Vec<BenchmarkFn>,
    concurrencies: Vec<usize>,
) {
    fx_log_info!("run benchmarks");

    let launcher = launcher().unwrap();
    let bindings: Vec<LaunchedBinding> =
        binding_configs.into_iter().map(|b| b.launch(&launcher)).collect();

    let results: Vec<_> = Benchmark::product(benchmarks, bindings, concurrencies)
        .into_iter()
        .map(|benchmark| {
            println!("Benchmarking {}", benchmark.name());
            for _ in 0..WARM_UP_RUNS {
                benchmark.run();
            }

            BenchmarkResult {
                label: benchmark.name(),
                test_suite: "fuchsia.fidl_microbenchmarks".to_string(),
                values: (0..MEASURE_RUNS).map(|_| benchmark.run()).collect(),
            }
        })
        .map(|result| result.json())
        .collect();

    let json = serde_json::to_string_pretty(&results).unwrap();
    let arguments: Vec<String> = env::args().collect();
    if arguments.len() > 1 {
        println!("Writing results to {}", arguments[1]);
        let mut file = File::create(format!("{}", arguments[1])).unwrap();
        file.write_all(json.as_bytes()).unwrap();
    } else {
        println!("json: {}", json);
    }
}

fn main() {
    global_executor::with(fasync::Executor::new().unwrap(), || {
        flog::init().unwrap();
        fx_log_info!("runner starting");

        let binding_configs =
            vec![BindingConfig {
            name: "LLCPP",
            url: "fuchsia-pkg://fuchsia.com/llcpp-roundtrip-fidl-benchmarks#meta/llcpp-roundtrip-fidl-benchmarks.cmx",
        },BindingConfig {
            name: "Go",
            url: "fuchsia-pkg://fuchsia.com/go-roundtrip-fidl-benchmarks#meta/go-roundtrip-fidl-benchmarks.cmx",
        }];

        run_benchmarks(binding_configs, benchmarks::all(), vec![1, 100]);
    });
}
