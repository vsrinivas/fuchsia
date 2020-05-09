// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod cmd;
mod label;
mod output;

use cmd::BenchmarkResult;
use label::LabelSpecPart::*;
use label::*;
use std::fmt;
use std::fs::File;
use std::io::{stdout, Write};
use std::path::Path;
use std::str::FromStr;
use structopt::StructOpt;

#[derive(PartialEq, Eq)]
enum BenchmarkType {
    Llcpp,
    Hlcpp,
    Go,
    Rust,
}

impl fmt::Display for BenchmarkType {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        fmt.write_str(match self {
            BenchmarkType::Llcpp => "llcpp",
            BenchmarkType::Hlcpp => "hlcpp",
            BenchmarkType::Go => "go",
            BenchmarkType::Rust => "rust",
        })
    }
}

impl FromStr for BenchmarkType {
    type Err = std::io::Error;

    fn from_str(s: &str) -> Result<BenchmarkType, std::io::Error> {
        match s {
            "llcpp" => Ok(BenchmarkType::Llcpp),
            "hlcpp" => Ok(BenchmarkType::Hlcpp),
            "go" => Ok(BenchmarkType::Go),
            "rust" => Ok(BenchmarkType::Rust),
            _ => Err(std::io::Error::new(std::io::ErrorKind::Other, "invalid benchmark type")),
        }
    }
}

struct Benchmark {
    name: BenchmarkType,
    cmd: &'static str,
    args: Vec<&'static str>,
    // Rules to duplicate benchmark results with transformed labels.
    transform_specs: Vec<LabelTransformSpec<'static>>,
}

impl Benchmark {
    pub fn run(&self) -> Vec<BenchmarkResult> {
        println!("-- Benchmark: {} --", self.name);
        let mut results: Vec<BenchmarkResult> = cmd::run(self.cmd, &self.args);
        for i in 0..results.len() {
            for transform_spec in &self.transform_specs {
                if let Some(replaced) = transform_spec.transform(&results[i].label) {
                    let mut copied_result = results[i].clone();
                    copied_result.label = replaced;
                    results.push(copied_result)
                }
            }
        }
        results
    }
}

fn standard_label_spec<'a>(name: &'a str) -> LabelSpec<'a> {
    LabelSpec::new(vec![Binding, Literal(name), BenchmarkCase, Measurement])
}

fn benchmarks() -> Vec<Benchmark> {
    vec![
        Benchmark {
            name: BenchmarkType::Go,
            cmd: "/bin/go_fidl_microbenchmarks",
            args: vec!["--encode_counts", "--out_file"],
            transform_specs: vec![],
        },
        Benchmark {
            name: BenchmarkType::Hlcpp,
            cmd: "/bin/hlcpp_fidl_microbenchmarks",
            args: vec!["-p", "--quiet", "--out"],
            transform_specs: vec![
                LabelTransformSpec {
                    from: LabelSpec::new(vec![
                        Binding,
                        Literal("Encode"),
                        BenchmarkCase,
                        Literal("Steps.Encode"),
                        Measurement,
                    ]),
                    to: standard_label_spec("Encode"),
                },
                LabelTransformSpec {
                    from: LabelSpec::new(vec![
                        Binding,
                        Literal("Decode"),
                        BenchmarkCase,
                        Literal("Steps.Decode"),
                        Measurement,
                    ]),
                    to: standard_label_spec("Decode"),
                },
            ],
        },
        Benchmark {
            name: BenchmarkType::Llcpp,
            cmd: "/bin/llcpp_fidl_microbenchmarks",
            args: vec!["-p", "--quiet", "--out"],
            transform_specs: vec![
                LabelTransformSpec {
                    from: LabelSpec::new(vec![
                        Binding,
                        Literal("Builder"),
                        BenchmarkCase,
                        Literal("Heap"),
                        Measurement,
                    ]),
                    to: standard_label_spec("Builder"),
                },
                LabelTransformSpec {
                    from: LabelSpec::new(vec![
                        Binding,
                        Literal("Encode"),
                        BenchmarkCase,
                        Literal("Steps.Encode"),
                        Measurement,
                    ]),
                    to: standard_label_spec("Encode"),
                },
                LabelTransformSpec {
                    from: LabelSpec::new(vec![
                        Binding,
                        Literal("Decode"),
                        BenchmarkCase,
                        Literal("Steps.Decode"),
                        Measurement,
                    ]),
                    to: standard_label_spec("Decode"),
                },
            ],
        },
        Benchmark {
            name: BenchmarkType::Rust,
            cmd: "/bin/rust_fidl_microbenchmarks",
            args: vec![],
            transform_specs: vec![],
        },
    ]
}

/// Filters results to only include standard results.
fn only_standard_results(inputs: Vec<BenchmarkResult>) -> Vec<BenchmarkResult> {
    let standard_label_specs = vec![
        standard_label_spec("Builder"),
        standard_label_spec("Encode"),
        standard_label_spec("Decode"),
    ];
    let mut results = Vec::new();
    for input in inputs {
        for label_spec in standard_label_specs.iter() {
            if label_spec.parse(&input.label).is_some() {
                results.push(input.clone());
            }
        }
    }
    results
}

/// Search for a pattern in a file and display the lines that contain it.
#[derive(StructOpt)]
#[structopt(about = "Runs fidl microbenchmarks")]
struct Cli {
    #[structopt(
        short = "o",
        long = "output-file",
        help = "if set, output will be written to the specified file"
    )]
    output_file: Option<String>,

    #[structopt(short = "f", long = "output-format", help = "selects output format")]
    output_format: output::OutputFormat,

    #[structopt(
        long = "include-nonstandard",
        help = "include nonstandard benchmarks in the output"
    )]
    include_nonstandard: bool,

    #[structopt(short = "b", long = "benchmarks", help = "the benchmarks to be run")]
    benchmarks: Vec<BenchmarkType>,
}

fn main() {
    let args = Cli::from_args();

    let mut results: Vec<BenchmarkResult> = Vec::new();
    for benchmark in benchmarks() {
        let num_matching = args.benchmarks.iter().filter(|b| **b == benchmark.name).count();
        if args.benchmarks.len() > 0 && num_matching == 0 {
            continue;
        }
        results.append(&mut benchmark.run());
    }
    if !args.include_nonstandard {
        results = only_standard_results(results)
    }

    let mut writer: Box<dyn Write> = match args.output_file {
        Some(filename) => {
            println!("writing to {}", filename);
            let path = Path::new(&filename);
            Box::new(File::create(&path).unwrap())
        }
        None => Box::new(stdout()),
    };

    output::write(&mut writer, results, args.output_format);
}
