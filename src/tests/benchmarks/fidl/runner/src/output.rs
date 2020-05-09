// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::cmd::BenchmarkResult;
use crate::label::{LabelSpec, LabelSpecPart};
use serde::Serialize;
use serde_json;
use std::collections::BTreeMap;
use std::io::Write;
use std::str::FromStr;

pub enum OutputFormat {
    Json,
    Csv,
}

impl FromStr for OutputFormat {
    type Err = std::io::Error;

    fn from_str(s: &str) -> Result<OutputFormat, std::io::Error> {
        match s {
            "json" => Ok(OutputFormat::Json),
            "csv" => Ok(OutputFormat::Csv),
            _ => Err(std::io::Error::new(std::io::ErrorKind::Other, "invalid output type")),
        }
    }
}

pub fn write(w: &mut dyn Write, results: Vec<BenchmarkResult>, output_format: OutputFormat) {
    match output_format {
        OutputFormat::Json => write_json(w, results),
        OutputFormat::Csv => write_csv(w, results),
    }
}

fn write_json(w: &mut dyn Write, results: Vec<BenchmarkResult>) {
    serde_json::to_writer_pretty(w, &results).expect("failed to serialize to json");
}

#[derive(Debug, Serialize, Clone, PartialEq)]
struct CsvRecord {
    benchmark_name: String,
    llcpp: Option<f64>,
    hlcpp: Option<f64>,
    rust: Option<f64>,
    go: Option<f64>,
}

fn build_csv_records(inputs: Vec<BenchmarkResult>) -> Vec<CsvRecord> {
    let match_spec = LabelSpec::new(vec![LabelSpecPart::Binding, LabelSpecPart::Any]);
    let binding_spec = LabelSpec::new(vec![LabelSpecPart::Binding]);
    let any_spec = LabelSpec::new(vec![LabelSpecPart::Any]);
    let mut records_by_benchmark: BTreeMap<String, CsvRecord> = BTreeMap::new();
    for input in inputs {
        let matched = match match_spec.parse(&input.label) {
            None => {
                println!("omitting from CSV: {}", input.label);
                continue;
            }
            Some(m) => m,
        };
        let binding = binding_spec.build_str(&matched).unwrap();
        let benchmark_name = any_spec.build_str(&matched).unwrap();
        let mean = input.values.iter().sum::<f64>() / input.values.len() as f64;

        let mut record = match records_by_benchmark.get_mut(&benchmark_name) {
            Some(record) => record.clone(),
            None => CsvRecord {
                benchmark_name: benchmark_name.clone(),
                llcpp: None,
                hlcpp: None,
                rust: None,
                go: None,
            },
        };
        std::io::stdout().flush().unwrap();
        match binding.as_str() {
            "LLCPP" => {
                record.llcpp = Some(mean);
            }
            "HLCPP" => {
                record.hlcpp = Some(mean);
            }
            "Rust" => {
                record.rust = Some(mean);
            }
            "Go" => {
                record.go = Some(mean);
            }
            _ => panic!("unhandled binding"),
        };
        records_by_benchmark.insert(benchmark_name, record);
    }
    records_by_benchmark.values().cloned().collect()
}

fn write_csv(w: &mut dyn Write, results: Vec<BenchmarkResult>) {
    let mut csv_writer = csv::Writer::from_writer(w);
    let records = build_csv_records(results);
    for record in records {
        csv_writer.serialize(record).expect("failed to write csv record");
    }
    csv_writer.flush().expect("failed to flush csv records");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_build_csv_records() {
        let input = vec![
            BenchmarkResult {
                label: "Go/Encode/MyStruct/WallTime".to_string(),
                test_suite: "".to_string(),
                unit: "".to_string(),
                values: vec![1.0],
                split_first: None,
            },
            BenchmarkResult {
                label: "Go/Decode/MyStruct/WallTime".to_string(),
                test_suite: "".to_string(),
                unit: "".to_string(),
                values: vec![0.0],
                split_first: None,
            },
            BenchmarkResult {
                label: "Rust/Encode/MyStruct/WallTime".to_string(),
                test_suite: "".to_string(),
                unit: "".to_string(),
                values: vec![1.0, 3.0],
                split_first: None,
            },
        ];
        let output = build_csv_records(input);
        let expected = vec![
            CsvRecord {
                benchmark_name: "Decode/MyStruct/WallTime".to_string(),
                llcpp: None,
                hlcpp: None,
                rust: None,
                go: Some(0.0),
            },
            CsvRecord {
                benchmark_name: "Encode/MyStruct/WallTime".to_string(),
                llcpp: None,
                hlcpp: None,
                rust: Some(2.0),
                go: Some(1.0),
            },
        ];
        assert_eq!(output, expected);
    }
}
