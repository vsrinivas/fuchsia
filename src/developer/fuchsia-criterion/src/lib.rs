// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Thin wrapper crate around the [Criterion benchmark suite].
//!
//! In order to facilitate micro-benchmarking Rust code on Fuchsia while at the same time providing
//! a pleasant experience locally, this crate provides a wrapper type [`FuchsiaCriterion`] that
//! implements `Deref<Target = Criterion>` with two constructors.
//!
//! By default, calling [`FuchsiaCriterion::default`] will create a [`Criterion`] object whose
//! output is saved to a command-line-provided JSON file that matches the
//! [Fuchsia benchmarking schema].
//!
//! In order to benchmark locally, i.e. in an `fx shell`, simply pass `--args=local_bench='true'` to
//! the `fx set` command which will create a CMD-configurable Criterion object that is useful for
//! fine-tuning performance.
//! ```no_run
//! # use fuchsia_criterion::FuchsiaCriterion;
//! #
//! fn main() {
//!     let mut _c = FuchsiaCriterion::default();
//! }
//! ```
//!
//! [Criterion benchmark suite]: https://github.com/bheisler/criterion.rs
//! [default]: https://doc.rust-lang.org/std/default/trait.Default.html#tymethod.default
//! [Fuchsia benchmarking schema]: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/development/benchmarking/results_schema.md

#![deny(missing_docs)]

use std::{
    collections::HashMap,
    env,
    fs::OpenOptions,
    io::Write,
    ops::{Deref, DerefMut},
    path::{Path, PathBuf},
    process,
};

use criterion::Criterion;
use csv;
use tempfile::TempDir;
use walkdir::WalkDir;

pub use criterion;

/// Thin wrapper around [`Criterion`].
pub struct FuchsiaCriterion {
    criterion: Criterion,
    output: Option<(TempDir, PathBuf)>,
}

impl FuchsiaCriterion {
    /// Creates a new [`Criterion`] wrapper.
    pub fn new(criterion: Criterion) -> Self {
        Self { criterion, output: None }
    }

    /// Creates a new [`Criterion`] object whose output is tailored to Fuchsia-CI.
    ///
    /// It calls its [`Default::default`] constructor with 10,000 resamples, then looks for a CMD
    /// argument providing the path for JSON file where the micro-benchmark results should be
    /// stored. The format respects the [Fuchsia benchmarking schema].
    ///
    /// [Fuchsia benchmarking schema]: https://fuchsia.googlesource.com/fuchsia/+/refs/heads/master/docs/development/benchmarking/results_schema.md
    pub fn fuchsia_bench() -> Self {
        fn help_and_exit(name: &str, wrong_args: Option<String>) -> ! {
            println!(
                "fuchsia-criterion benchmark\n\
                 \n\
                 USAGE: {} [FLAGS] JSON_OUTPUT\n\
                 \n\
                 FLAGS:\n\
                 -h, --help    Prints help information",
                name,
            );

            if let Some(wrong_args) = wrong_args {
                eprintln!("error: unrecognized args: {}", wrong_args);
                process::exit(1)
            } else {
                process::exit(0)
            }
        }

        let args: Vec<String> = env::args().collect();
        let args: Vec<&str> = args.iter().map(|s| &**s).collect();

        match &args[..] {
            [_, arg] if !arg.starts_with('-') => {
                let output_directory =
                    TempDir::new().expect("failed to access temporary directory");
                let criterion = Criterion::default()
                    .nresamples(10_000)
                    .output_directory(output_directory.path());

                Self {
                    criterion,
                    output: Some((output_directory, Path::new(&args[1]).to_path_buf())),
                }
            }
            [name, "-h"] | [name, "--help"] => help_and_exit(name, None),
            _ => help_and_exit(&args[0], Some(args[1..].join(" "))),
        }
    }

    fn convert_csvs(output_directory: &Path, output_json: &Path) {
        let csv_entries = WalkDir::new(output_directory).into_iter().filter_map(|res| {
            res.ok().filter(|entry| {
                let path = entry.path();
                path.is_file() && path.extension().map(|ext| ext == "csv").unwrap_or(false)
            })
        });

        let mut results: HashMap<_, Vec<f64>> = HashMap::new();

        for csv_entry in csv_entries {
            let mut reader = csv::Reader::from_path(csv_entry.path())
                .expect("found non-CSV file in fuchsia-criterion specific temporary directory");

            for record in reader.records() {
                let record = record.expect("CSV record is not UTF-8");

                if record.len() != 5 {
                    panic!("wrong number of records in Criterion-generated CSV");
                }

                let label = record[1].to_string();
                let test_suite = record[0].to_string();
                let sample = match (record[3].parse::<f64>(), record[4].parse::<f64>()) {
                    (Ok(time), Ok(iter_count)) => time / iter_count,
                    _ => panic!("non floating point values in Criterion-generated CSV"),
                };

                results
                    .entry((label, test_suite))
                    .and_modify(|samples| samples.push(sample))
                    .or_insert_with(|| vec![sample]);
            }
        }

        let entries: Vec<_> = results
            .into_iter()
            .map(|((label, test_suite), samples)| {
                format!(
                    r#"{{
        "label": {:?},
        "test_suite": {:?},
        "unit": "nanoseconds",
        "values": {:?}
    }}"#,
                    label, test_suite, samples,
                )
            })
            .collect();

        let mut f = OpenOptions::new()
            .truncate(true)
            .write(true)
            .create(true)
            .open(output_json)
            .expect("failed to open output JSON");
        write!(f, "[\n    {}\n]\n", entries.join(",\n"),).expect("failed to write to output JSON");
    }
}

impl Default for FuchsiaCriterion {
    fn default() -> Self {
        if cfg!(feature = "local_bench") {
            let criterion = Criterion::default()
                .nresamples(10_000)
                .output_directory(Path::new("/tmp/criterion"))
                .configure_from_args();

            FuchsiaCriterion::new(criterion)
        } else {
            FuchsiaCriterion::fuchsia_bench()
        }
    }
}

impl Deref for FuchsiaCriterion {
    type Target = Criterion;

    fn deref(&self) -> &Self::Target {
        &self.criterion
    }
}

impl DerefMut for FuchsiaCriterion {
    fn deref_mut(&mut self) -> &mut Self::Target {
        &mut self.criterion
    }
}

impl Drop for FuchsiaCriterion {
    /// Drops the [`Criterion`] wrapper.
    ///
    /// If initialized with [`FuchsiaCriterion::fuchsia_bench`], it will write the Fuchsia-specific
    /// JSON file before dropping.
    fn drop(&mut self) {
        self.criterion.final_summary();

        if let Some((output_directory, output_json)) = &self.output {
            Self::convert_csvs(output_directory.path(), output_json);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    use std::{
        fs::{self, File},
        io,
    };

    #[test]
    fn criterion_results_conversion() -> io::Result<()> {
        let temp = TempDir::new()?;
        let output_directory = temp.path().join("some/where/deep");

        fs::create_dir_all(output_directory.clone())?;

        let mut csv = File::create(output_directory.join("criterion.csv"))?;
        csv.write_all(
            b"\
            group,function,value,sample_time_nanos,iteration_count\n\
            fib,parallel,,1000000,100000\n\
            fib,parallel,,2000000,200000\n\
            fib,parallel,,4000000,300000\n\
            fib,parallel,,6000000,400000\n\
        ",
        )?;

        let json = output_directory.join("fuchsia.json");
        FuchsiaCriterion::convert_csvs(&output_directory, &json);

        assert_eq!(
            fs::read_to_string(json)?,
            r#"[
    {
        "label": "parallel",
        "test_suite": "fib",
        "unit": "nanoseconds",
        "values": [10.0, 10.0, 13.333333333333334, 15.0]
    }
]
"#
        );

        Ok(())
    }
}
