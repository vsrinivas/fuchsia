// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, diagnostics_data::Severity, ffx_core::ffx_command};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "test",
    description = "Run test suite",
    note = "Run tests or inspect output from a previous test run."
)]
pub struct TestCommand {
    #[argh(subcommand)]
    pub subcommand: TestSubcommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum TestSubcommand {
    Run(RunCommand),
    List(ListCommand),
    Result(ResultCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "run",
    description = "Run test suite",
    note = "Runs a test or suite implementing the `fuchsia.test.Suite` protocol.

Note that if running multiple iterations of a test and an iteration times
out, no further iterations will be executed."
)]
pub struct RunCommand {
    /// test timeout
    #[argh(option, short = 't')]
    pub timeout: Option<u32>,

    /// test url
    #[argh(positional)]
    pub test_url: String,

    /// a glob pattern for matching tests
    #[argh(option)]
    pub test_filter: Option<String>,

    /// run tests that have been marked disabled/ignored
    #[argh(switch)]
    pub run_disabled: bool,

    /// filter ANSI escape sequences from output
    #[argh(switch)]
    pub filter_ansi: bool,

    /// run tests in parallel
    #[argh(option)]
    pub parallel: Option<u16>,

    /// number of times to run the test [default = 1]
    #[argh(option)]
    pub count: Option<u16>,

    /// when set, only logs with a severity equal to the given one or higher will be printed.
    #[argh(option)]
    pub min_severity_logs: Option<Severity>,

    /// when set, the test will fail if any log with a higher severity is emitted.
    #[argh(option)]
    pub max_severity_logs: Option<Severity>,

    /// when set, output test results to the specified directory.
    #[argh(option)]
    pub experimental_output_directory: Option<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list",
    description = "List test suite cases",
    note = "Lists the set of test cases available in a test suite"
)]
pub struct ListCommand {
    /// test url
    #[argh(positional)]
    pub test_url: String,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "result",
    description = "Manage test results",
    note = "Inspect and manage the results from previous test runs"
)]
pub struct ResultCommand {}
