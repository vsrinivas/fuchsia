// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod data; // Inspect data maintainer/updater/scanner-from-vmo; compare engine
mod metrics; // Evaluates memory performance of Inspect library
mod puppet; // Interface to target Inspect library wrapper programs (puppets)
mod results; // Stores and formats reports-to-user
mod runner; // Coordinates testing operations
mod trials; // Defines the trials to run

use {
    anyhow::{format_err, Error},
    argh::FromArgs,
    fidl_test_inspect_validate as validate,
    serde::Serialize,
    std::str::FromStr,
};

/// meta/validator.shard.cml must use this name in `children: name:`.
const PUPPET_MONIKER: &str = "puppet";

/// Validate Inspect VMO formats written by 'puppet' programs controlled by
/// this Validator program and exercising Inspect library implementations.
//#[derive(StructOpt, Debug)]
#[derive(Debug, FromArgs)]
struct Opt {
    /// report results in a pretty human-readable format. Without this flag,
    /// results will be printed as JSON.
    #[argh(option, long = "output", default = "OutputType::Json")]
    output: OutputType,

    /// when trees differ, render 'full' text, 'diff' type difference, or 'both'.
    #[argh(option, long = "difftype", default = "DiffType::Full")]
    diff_type: DiffType,

    /// required arg(s): A printable name to describe the test output.
    #[argh(option, long = "printable-name")]
    printable_name: String,

    /// has no effect.
    // TODO(fxbug.dev/84729)
    #[argh(switch, long = "quiet")]
    #[allow(unused)]
    quiet: bool,

    /// has no effect.
    // TODO(fxbug.dev/84729)
    #[argh(switch, long = "verbose")]
    #[allow(unused)]
    verbose: bool,

    /// prints the version information and exits.
    #[argh(switch, long = "version", short = 'v')]
    version: bool,

    /// because the Dart runner adds a "runner" node to the hierarchy
    /// that needs to be removed before comparison.
    #[argh(switch, long = "is-dart")]
    is_dart: bool,

    /// tests that the Archive FIDL service output matches the expected values.
    #[argh(switch)]
    test_archive: bool,

    /// this test validates golang, so we need to apply special workarounds for its
    /// timing requirements.
    #[argh(switch, long = "golang", short = 'g')]
    golang: bool,
}

#[derive(Debug)]
enum OutputType {
    Text,
    Json,
}

impl FromStr for OutputType {
    type Err = Error;
    fn from_str(s: &str) -> Result<OutputType, Error> {
        Ok(match s {
            "text" => OutputType::Text,
            "json" => OutputType::Json,
            _ => return Err(format_err!("Output type must be 'text' or 'json'")),
        })
    }
}

/// When reporting a discrepancy between local and remote Data trees, should the output include:
/// - The full rendering of both trees?
/// - The condensed diff between the trees? (This may still be quite large.)
/// - Both full and condensed renderings?
#[derive(Clone, Copy, Debug, Serialize)]
pub enum DiffType {
    Full,
    Diff,
    Both,
}

impl FromStr for DiffType {
    type Err = Error;
    fn from_str(s: &str) -> Result<DiffType, Error> {
        Ok(match s {
            "full" => DiffType::Full,
            "diff" => DiffType::Diff,
            "both" => DiffType::Both,
            _ => return Err(format_err!("Diff type must be 'full' or 'diff' or 'both'")),
        })
    }
}

#[fuchsia::main]
async fn main() -> Result<(), Error> {
    let mut results = results::Results::new();
    let Opt { output, printable_name, version, is_dart, diff_type, test_archive, golang, .. } =
        argh::from_env();
    if version {
        println!("Inspect Validator version 0.9. See README.md for more information.");
        return Ok(());
    }
    results.diff_type = diff_type;
    results.test_archive = test_archive;

    runner::run_all_trials(&printable_name, is_dart, golang, &mut results).await;
    match output {
        OutputType::Text => results.print_pretty_text(),
        OutputType::Json => println!("{}", results.to_json()),
    }
    if results.failed() {
        return Err(format_err!("A test failed"));
    } else {
        Ok(())
    }
}

// The only way to test this file is to actually start a component, and that's
// not suitable for unit tests. Failures will be caught in integration
// tests.
