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
    pub subcommand: TestSubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum TestSubCommand {
    Run(RunCommand),
    List(ListCommand),
    Result(ResultCommand),
}

#[derive(FromArgs, Debug, PartialEq, Clone)]
#[argh(
    subcommand,
    name = "run",
    description = "Execute test suites on a target device",
    example = "\
Run a test suite:
    $ ffx test run fuchsia-pkg://fuchsia.com/my_test#meta/my_test.cm

Run a test suite and pass arguments to the suite:
    $ ffx test run fuchsia-pkg://fuchsia.com/my_test#meta/my_test.cm -- arg1 arg2

Run test suites specified in a JSON file (currently unstable):
    $ ffx test run --test-file test-list.json

Given a suite that contains the test cases 'Foo.Test1', 'Foo.Test2',
'Bar.Test1', and 'Bar.Test2':

Run test cases that start with 'Foo.' ('Foo.Test1', 'Foo.Test2'):
    $ ffx test run <suite-url> --test-filter 'Foo.*'

Run test cases that do not start with 'Foo.' ('Bar.Test1', 'Bar.Test2'):
    $ ffx test run <suite-url> --test-filter '-Foo.*'

Run test cases that start with 'Foo.' and do not end with 'Test1' ('Foo.Test2'):
    $ ffx test run <suite-url> --test-filter 'Foo.*' --test-filter '-*.Test1'",
    note = "Runs test suites implementing the `fuchsia.test.Suite` protocol.

When multiple test suites are run, either through the --count option or through
--test-file, the default behavior of ffx test is:
    If any test suite times out, halt execution and do not attempt to run any
    unstarted suites.
    If any test suite fails for any other reason, continue to run unstarted
    suites."
)]
pub struct RunCommand {
    /// test suite url, and any arguments passed to tests, following `--`.
    /// When --test-file is specified test_args should not be specified.
    #[argh(positional)]
    pub test_args: Vec<String>,

    // TODO(satsukiu): once stable, document the format
    /// read test url and options from the specified file instead of from the
    /// command line.
    /// Stdin may be specified by passing `--test-file -`.
    /// May not be used in conjunction with `test_args`, `--test-file`,
    /// `--test-filter`, `--run-disabled`, `--parallel`, `--max-severity-logs`
    /// This option is currently unstable and the format of the file is subject
    /// to change. Using this option requires setting the
    /// 'test.experimental_json_input' configuration to true.
    ///
    /// For current details, see test-list.json format at
    /// https://fuchsia.googlesource.com/fuchsia/+/refs/heads/main/src/lib/testing/test_list/
    #[argh(option)]
    pub test_file: Option<String>,

    // suite options
    /// test suite timeout in seconds.
    #[argh(option, short = 't')]
    pub timeout: Option<u32>,

    /// test case filter. This filter will match based on glob pattern. This
    /// option may be specified multiple times. Only test cases matching at
    /// least one pattern will be run. Negative filters may be specified by
    /// prepending '-' and will exclude matching test cases.
    #[argh(option)]
    pub test_filter: Vec<String>,

    /// also execute test cases that have been disabled by the test author.
    #[argh(switch)]
    pub run_disabled: bool,

    /// maximum number of test cases to run in parallel. Defaults to a value
    /// specified by the test runner.
    #[argh(option)]
    pub parallel: Option<u16>,

    /// when set, fails tests that emit logs with a higher severity.
    ///
    /// For example, if --max-severity-logs WARN is specified, fails any test
    /// that produces an ERROR level log.
    #[argh(option)]
    pub max_severity_logs: Option<Severity>,

    // test run options
    /// continue running unfinished suites if a suite times out.
    /// This option is only relevant when multiple suites are run.
    #[argh(switch)]
    pub continue_on_timeout: bool,

    /// stop running unfinished suites after the number of provided failures
    /// has occurred. This option is only relevant when multiple suites are
    /// run.
    #[argh(option)]
    pub stop_after_failures: Option<u32>,

    /// number of times to run the test suite. By default run the suite 1 time.
    #[argh(option)]
    pub count: Option<u32>,

    /// enables experimental parallel test scheduler. The provided number
    /// specifies the max number of test suites to run in parallel.
    /// If the value provided is 0, a default value will be chosen by the
    /// server implementation.
    #[argh(option)]
    pub experimental_parallel_execution: Option<u16>,

    // output options
    /// filter ANSI escape sequences from output.
    #[argh(switch)]
    pub filter_ansi: bool,

    /// set the minimum log severity printed. Must be one of
    /// FATAL|ERROR|WARN|INFO|DEBUG|TRACE.
    #[argh(option)]
    pub min_severity_logs: Option<Severity>,

    /// show the full moniker in unstructured log output.
    #[argh(switch)]
    pub show_full_moniker_in_logs: bool,

    /// output test results to the specified directory. The produced output
    /// is in the format described in
    /// https://fuchsia.dev/fuchsia-src/reference/platform-spec/testing/test-output-format
    #[argh(option)]
    pub output_directory: Option<String>,

    /// disable structured output to a directory. Note that normally,
    /// structured output is disabled by default and is only produced when
    /// --output-directory is specified. --disable-output-directory is only
    /// relevant when the 'test.experimental_managed_structured_output'
    /// experimental configuration is set true, which changes the default
    /// behavior to produce structured output.
    #[argh(switch)]
    pub disable_output_directory: bool,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list-cases",
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
pub struct ResultCommand {
    #[argh(subcommand)]
    pub subcommand: ResultSubCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
pub enum ResultSubCommand {
    Show(ShowResultCommand),
    List(ListResultCommand),
    Delete(DeleteResultCommand),
    Save(SaveResultCommand),
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "show",
    description = "Display test results",
    note = "Display the results of a previous test run.

When no options are provided, displays the results of the most recent test run.
"
)]
pub struct ShowResultCommand {
    /// when set, display the results of the specified directory.
    #[argh(option)]
    pub directory: Option<String>,
    /// when set, display the results of a run with specified index.
    #[argh(option)]
    pub index: Option<u32>,
    /// when set, display the results of a run with specified name.
    #[argh(option)]
    pub name: Option<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "list",
    description = "List test results",
    note = "Display a list of previous test runs"
)]
pub struct ListResultCommand {}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "delete",
    description = "Delete test results",
    note = "Manually delete a previous test run result.

Either --index or --name must be specified."
)]
pub struct DeleteResultCommand {
    /// when set, display the results of a run with specified index.
    #[argh(option)]
    pub index: Option<u32>,
    /// when set, display the results of a run with specified name.
    #[argh(option)]
    pub name: Option<String>,
}

#[derive(FromArgs, Debug, PartialEq)]
#[argh(
    subcommand,
    name = "save",
    description = "Save test results",
    note = "Mark a test result so it is not garbage collected.

By default, ffx test only retains the last 'n' test results, as configured
with 'test.save_count'. A test result saved and given a name using the save
command will be exempted from this cleanup and will not be counted towards the
total number of saved test results."
)]
pub struct SaveResultCommand {
    /// the index of the test result to save.
    #[argh(option)]
    pub index: u32,
    /// the name to assign the saved test result.
    #[argh(option)]
    pub name: String,
}
