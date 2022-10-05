// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use assert_matches::assert_matches;
use diagnostics_data::Severity;
use diagnostics_reader::{ArchiveReader, Inspect, Property};
use fidl_fuchsia_test_manager::{LaunchError, LogsIteratorOption, RunBuilderMarker};
use regex::Regex;
use run_test_suite_lib::{output, Outcome, RunTestSuiteError, TestParams};
use std::convert::TryInto;
use std::ops::Deref;
use std::str::from_utf8;
use std::sync::Arc;
use test_case::test_case;
use test_list::TestTag;
use test_output_directory::{
    self as directory,
    testing::{ExpectedDirectory, ExpectedSuite, ExpectedTestCase, ExpectedTestRun},
};
use test_util::assert_geq;

/// split and sort output as output can come in any order.
/// `output` is of type vec<u8> and `expected_output` is a string.
macro_rules! assert_output {
    ($output:expr, $expected_output:expr) => {
        let mut expected_output = $expected_output.split("\n").collect::<Vec<_>>();

        // no need to check for lines starting with "[stdout - " and "[stderr - ". We just want to
        // make sure that we are printing important details.
        let mut output = from_utf8(&$output)
            .expect("we should not get utf8 error.")
            .split("\n")
            .filter(|x| {
                let is_stdout = x.starts_with("[stdout - ");
                let is_stderr = x.starts_with("[stderr - ");
                let is_debug_data_warn = is_debug_data_warning(x);
                !(is_stdout || is_stderr || is_debug_data_warn)
            })
            .map(sanitize_log_for_comparison)
            .collect::<Vec<_>>();

        expected_output.sort();
        output.sort();

        assert_eq!(output, expected_output);
    };
}

// TODO(fxbug.dev/61180): once debug data routing is fixed, this log should be gone and this
// function can be deleted.
fn is_debug_data_warning(log: impl AsRef<str>) -> bool {
    log.as_ref().contains("No capability available at path /svc/fuchsia.debugdata.Publisher")
}

fn sanitize_log_for_comparison(log: impl AsRef<str>) -> String {
    let log_timestamp_re = Regex::new(r"^\[\d+.\d+\]\[\d+\]\[\d+\]").unwrap();
    log_timestamp_re.replace_all(log.as_ref(), "[TIMESTAMP][PID][TID]").to_string()
}

fn new_test_params(test_url: &str) -> TestParams {
    TestParams {
        test_url: test_url.to_string(),
        timeout_seconds: None,
        test_filters: None,
        also_run_disabled_tests: false,
        parallel: None,
        test_args: vec![],
        max_severity_logs: None,
        show_full_moniker: true,
        tags: vec![TestTag { key: "internal".to_string(), value: "true".to_string() }],
    }
}

fn new_run_params() -> run_test_suite_lib::RunParams {
    run_test_suite_lib::RunParams {
        timeout_behavior: run_test_suite_lib::TimeoutBehavior::TerminateRemaining,
        stop_after_failures: None,
        experimental_parallel_execution: None,
        accumulate_debug_data: false,
        log_protocol: None,
    }
}

type TestShellReporter = run_test_suite_lib::output::ShellReporter<Vec<u8>>;
type TestMuxReporter = run_test_suite_lib::output::MultiplexedReporter<
    TestShellReporter,
    run_test_suite_lib::output::DirectoryReporter,
>;
type TestMuxMuxReporter = run_test_suite_lib::output::MultiplexedReporter<
    TestMuxReporter,
    run_test_suite_lib::output::ShellReporter<std::io::BufWriter<std::fs::File>>,
>;
type TestOutputView = run_test_suite_lib::output::ShellWriterView<Vec<u8>>;

fn create_shell_reporter() -> (TestShellReporter, TestOutputView) {
    run_test_suite_lib::output::ShellReporter::new_expose_writer_for_test()
}

fn create_dir_reporter() -> (run_test_suite_lib::output::DirectoryReporter, tempfile::TempDir) {
    let tmp_dir = tempfile::tempdir().unwrap();
    let dir_reporter = run_test_suite_lib::output::DirectoryReporter::new(
        tmp_dir.path().to_path_buf(),
        run_test_suite_lib::output::SchemaVersion::V1,
    )
    .unwrap();
    (dir_reporter, tmp_dir)
}

fn create_shell_and_dir_reporter() -> (TestMuxReporter, TestOutputView, tempfile::TempDir) {
    let (shell_reporter, output) = create_shell_reporter();
    let (dir_reporter, tmp_dir) = create_dir_reporter();
    (
        run_test_suite_lib::output::MultiplexedReporter::new(shell_reporter, dir_reporter),
        output,
        tmp_dir,
    )
}

/// Runs a test with a reporter that saves in the directory output format and shell output.
/// This also automatically saves the shell output to a file in custom artifacts.
async fn run_with_reporter<F, Fut>(case_name: &str, test_fn: F)
where
    F: FnOnce(TestMuxMuxReporter, TestOutputView, tempfile::TempDir) -> Fut,
    Fut: futures::future::Future<Output = ()>,
{
    let file_for_shell_reporter =
        std::fs::File::create(format!("custom_artifacts/{}_shell.txt", case_name)).unwrap();
    let file_reporter = run_test_suite_lib::output::ShellReporter::new(std::io::BufWriter::new(
        file_for_shell_reporter,
    ));
    let (mux_reporter, output, tmpdir) = create_shell_and_dir_reporter();
    let mux_mux_reporter =
        run_test_suite_lib::output::MultiplexedReporter::new(mux_reporter, file_reporter);
    test_fn(mux_mux_reporter, output, tmpdir).await;
}

/// run specified test once. Returns suite result and output to shell.
async fn run_test_once(
    reporter: impl 'static + run_test_suite_lib::output::Reporter + Send + Sync,
    test_params: TestParams,
    min_log_severity: Option<Severity>,
) -> Result<Outcome, RunTestSuiteError> {
    let run_reporter = run_test_suite_lib::output::RunReporter::new(reporter);
    Ok(run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params],
        new_run_params(),
        min_log_severity,
        run_reporter,
        futures::future::pending(),
    )
    .await)
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_no_clean_exit(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let outcome = run_test_once(
        reporter,
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/no-onfinished-after-test-example.cm",
            ),
        None,
    )
    .await
    .expect("Running test should not fail");

    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/no-onfinished-after-test-example.cm'
[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
log1 for Example.Test2
log2 for Example.Test2
log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
[PASSED]	Example.Test3

3 out of 3 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/no-onfinished-after-test-example.cm completed with result: INCONCLUSIVE
";
    assert_output!(output.lock().as_slice(), expected_output);

    assert_eq!(outcome, Outcome::Inconclusive);

    directory::testing::assert_run_result(
        output_dir.path(),
        &ExpectedTestRun::new(directory::Outcome::Inconclusive)
            .with_suite(
                ExpectedSuite::new(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/no-onfinished-after-test-example.cm",
                    directory::Outcome::Inconclusive,
                )
                .with_tag(TestTag::new("internal", "true"))
                .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed))
                .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed))
                .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed))
            )
    );
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_passing_v2_test(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                    .expect("connecting to RunBuilderProxy"),
        vec![new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
        )],
        new_run_params(),
        None,
        reporter,
        futures::future::pending(),
    )
    .await;

    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm'
[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
log1 for Example.Test2
log2 for Example.Test2
log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
[PASSED]	Example.Test3

3 out of 3 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm completed with result: PASSED
";
    assert_output!(output.lock().as_ref(), expected_output);

    assert_eq!(outcome, Outcome::Passed);

    directory::testing::assert_run_result(
        output_dir.path(),
        &ExpectedTestRun::new(directory::Outcome::Passed)
            .with_suite(
                ExpectedSuite::new(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
                    directory::Outcome::Passed,
                )
                .with_tag(TestTag::new("internal", "true"))
                .with_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), "")
                .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed).with_artifact(
                    directory::ArtifactType::Stdout,
                    "stdout.txt".into(),
                    "log1 for Example.Test1\nlog2 for Example.Test1\nlog3 for Example.Test1\n",
                ))
                .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed).with_artifact(
                    directory::ArtifactType::Stdout,
                    "stdout.txt".into(),
                    "log1 for Example.Test2\nlog2 for Example.Test2\nlog3 for Example.Test2\n",
                ))
                .with_case(
                    ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed).with_artifact(
                        directory::ArtifactType::Stdout,
                        "stdout.txt".into(),
                        "log1 for Example.Test3\nlog2 for Example.Test3\nlog3 for Example.Test3\n",
                    ),
                )
            )
    );
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
// TODO(fxbug.dev/106819): we should add a test case similar to this one where run_test_suite does
// not use experimental_parallel_execution. We currently don't have one because
// it is possible for a test run to persist because its controller terminated
// early, which may cause the test to fail depending on the implementation.
async fn experimental_parallel_execution_integ_test(
    reporter: TestMuxMuxReporter,
    _output: TestOutputView,
    _output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let mut run_params = new_run_params();
    run_params.experimental_parallel_execution = Some(8);
    let _outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                    .expect("connecting to RunBuilderProxy"),
        vec![new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
        )],
        run_params,
        None,
        reporter,
        futures::future::pending(),
    )
    .await;

    let data = ArchiveReader::new()
        .add_selector("test_manager:root")
        .snapshot::<Inspect>()
        .await
        .expect("got inspect data");
    assert_eq!(data[0].moniker, "test_manager");

    // Manually look through all of the children in the "finished"
    // DiagnosticsHierarchy to look for a run that
    // contains used_parallel_scheduler: true.
    let root = data[0].payload.as_ref().unwrap();
    let root_children = &root.children;

    let finished_node = root_children
        .iter()
        .filter(|child| child.name == "finished")
        .next()
        .expect("expected finished node");

    let finished_node_children = &finished_node.children;
    for child in finished_node_children {
        let grandchildren = &child.children;
        for grandchild in grandchildren {
            let properties = &grandchild.properties;
            let expected_property = Property::Bool("used_parallel_scheduler".to_string(), true);
            if properties.contains(&expected_property) {
                return;
            }
        }
    }
    panic!("Did not route parallel config as expected");
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_stderr_test(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/test-with-stderr.cm",
        )],
        new_run_params(),
        None,
        reporter,
        futures::future::pending(),
    )
    .await;

    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/test-with-stderr.cm'
[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
stderr msg1 for Example.Test1
stderr msg2 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
log1 for Example.Test2
log2 for Example.Test2
log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
stderr msg1 for Example.Test3
stderr msg2 for Example.Test3
[PASSED]	Example.Test3

3 out of 3 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/test-with-stderr.cm completed with result: PASSED
";
    assert_output!(output.lock().as_ref(), expected_output);

    assert_eq!(outcome, Outcome::Passed);
    directory::testing::assert_run_result(
        output_dir.path(),
        &ExpectedTestRun::new(directory::Outcome::Passed)
            .with_suite(
                ExpectedSuite::new(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/test-with-stderr.cm",
                    directory::Outcome::Passed,
                )
                .with_tag(TestTag::new("internal", "true"))
                .with_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), "")
                .with_case(
                    ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed)
                        .with_artifact(
                            directory::ArtifactType::Stdout,
                            "stdout.txt".into(),
                            "log1 for Example.Test1\nlog2 for Example.Test1\nlog3 for Example.Test1\n",
                        )
                        .with_artifact(
                            directory::ArtifactType::Stderr,
                            "stderr.txt".into(),
                            "stderr msg1 for Example.Test1\nstderr msg2 for Example.Test1\n",
                        ),
                )
                .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed).with_artifact(
                    directory::ArtifactType::Stdout,
                    "stdout.txt".into(),
                    "log1 for Example.Test2\nlog2 for Example.Test2\nlog3 for Example.Test2\n",
                ))
                .with_case(
                    ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed)
                        .with_artifact(
                            directory::ArtifactType::Stdout,
                            "stdout.txt".into(),
                            "log1 for Example.Test3\nlog2 for Example.Test3\nlog3 for Example.Test3\n",
                        )
                        .with_artifact(
                            directory::ArtifactType::Stderr,
                            "stderr.txt".into(),
                            "stderr msg1 for Example.Test3\nstderr msg2 for Example.Test3\n",
                        ),
                )
            )
    );
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_passing_v2_test_multiple_times(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                    .expect("connecting to RunBuilderProxy"),
            vec![new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
                ); 10],
                new_run_params(),
            None,
            reporter,
            futures::future::pending(),
        )
    .await;
    assert_eq!(outcome, Outcome::Passed);

    let expected_test_suite = ExpectedSuite::new(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
        directory::Outcome::Passed,
    )
    .with_tag(TestTag::new("internal", "true"))
    .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed))
    .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed))
    .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed));

    let suite_results =
        directory::TestRunResult::from_dir(output_dir.path()).expect("parse dir").suites;

    assert_eq!(suite_results.len(), 10);
    for suite_result in suite_results {
        directory::testing::assert_suite_result(
            output_dir.path(),
            &suite_result,
            &expected_test_suite,
        );
    }
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_multiple_passing_tests(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                    .expect("connecting to RunBuilderProxy"),
            vec![new_test_params(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
                ),
                new_test_params(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/test-with-stderr.cm",
                )
            ],
            new_run_params(),
            None,
            reporter,
            futures::future::pending(),
        )
    .await;
    assert_eq!(outcome, Outcome::Passed);

    let expected_test_run = ExpectedTestRun::new(directory::Outcome::Passed)
        .with_suite(
            ExpectedSuite::new(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
                directory::Outcome::Passed,
            )
            .with_tag(TestTag::new("internal", "true"))
            .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed))
            .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed))
            .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed)),
        )
        .with_suite(
            ExpectedSuite::new(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/test-with-stderr.cm",
                directory::Outcome::Passed,
            )
            .with_tag(TestTag::new("internal", "true"))
            .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed))
            .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed))
            .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed))
        );

    directory::testing::assert_run_result(output_dir.path(), &expected_test_run);
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_with_filter(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
    );

    test_params.test_filters = Some(vec!["*Test3".to_string()]);
    let outcome =
        run_test_once(reporter, test_params, None).await.expect("Running test should not fail");

    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm'
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
[PASSED]	Example.Test3

1 out of 1 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm completed with result: PASSED
";
    assert_output!(output.lock().as_slice(), expected_output);

    assert_eq!(outcome, Outcome::Passed);

    directory::testing::assert_run_result(
        output_dir.path(),
        &ExpectedTestRun::new(directory::Outcome::Passed)
            .with_suite(
                ExpectedSuite::new(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
                    directory::Outcome::Passed
                )
                .with_tag(TestTag::new("internal", "true"))
                .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed))
            )
    );
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_with_multiple_filter(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
    );

    test_params.test_filters = Some(vec!["*Test3".to_string(), "*Test1".to_string()]);
    let outcome =
        run_test_once(reporter, test_params, None).await.expect("Running test should not fail");

    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm'
[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
[PASSED]	Example.Test3

2 out of 2 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm completed with result: PASSED
";
    assert_output!(output.lock().as_slice(), expected_output);

    assert_eq!(outcome, Outcome::Passed);
    directory::testing::assert_run_result(
        output_dir.path(),
        &ExpectedTestRun::new(directory::Outcome::Passed)
            .with_suite(
                ExpectedSuite::new(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
                    directory::Outcome::Passed
                )
                .with_tag(TestTag::new("internal", "true"))
                .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed))
                .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed))
            )
    );
}

#[fuchsia::test]
async fn launch_with_filter_no_matching_cases() {
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
    );

    test_params.test_filters = Some(vec!["matches-nothing".to_string()]);
    let outcome = run_test_once(output::NoopReporter, test_params, None).await.unwrap();

    match outcome {
        Outcome::Error { origin } => {
            assert_matches!(
                origin.as_ref(),
                RunTestSuiteError::Launch(LaunchError::NoMatchingCases)
            );
            assert!(!origin.is_internal_error());
        }
        _ => panic!("Expected error but got {:?}", outcome),
    }
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_empty_test(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let outcome = run_test_once(
        reporter,
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/no-test-example.cm",
        ),
        None,
    )
    .await
    .unwrap();

    assert_eq!(outcome, Outcome::Passed);

    directory::testing::assert_run_result(
        output_dir.path(),
        &ExpectedTestRun::new(directory::Outcome::Passed)
            .with_suite(
                ExpectedSuite::new(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/no-test-example.cm",
                    directory::Outcome::Passed,
                )
                .with_tag(TestTag::new("internal", "true"))
            )
    );
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_huge_test(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let outcome = run_test_once(
        reporter,
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/huge-test-example.cm",
        ),
        None,
    )
    .await
    .unwrap();

    assert_eq!(outcome, Outcome::Passed);

    let mut expected_test_suite = ExpectedSuite::new(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/huge-test-example.cm",
        directory::Outcome::Passed,
    )
    .with_tag(TestTag::new("internal", "true"));
    for i in 1..1001 {
        expected_test_suite = expected_test_suite.with_case(ExpectedTestCase::new(
            format!("FooTest{:?}", i),
            directory::Outcome::Passed,
        ));
    }

    directory::testing::assert_run_result(
        output_dir.path(),
        &ExpectedTestRun::new(directory::Outcome::Passed).with_suite(expected_test_suite),
    );
}

#[test_case(LogsIteratorOption::BatchIterator ; "batch")]
#[test_case(LogsIteratorOption::ArchiveIterator ; "archive")]
#[fuchsia::test]
async fn launch_and_test_logspam_test(iterator_option: LogsIteratorOption) {
    let (reporter, _output, output_dir) = create_shell_and_dir_reporter();

    let mut run_params = new_run_params();
    run_params.log_protocol = Some(iterator_option);
    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logspam_test.cm",
        )],
        run_params,
        None,
        run_test_suite_lib::output::RunReporter::new(reporter),
        futures::future::pending(),
    )
    .await;
    assert_eq!(outcome, Outcome::Passed);

    directory::testing::assert_run_result(
        output_dir.path(),
        &ExpectedTestRun::new(directory::Outcome::Passed).with_suite(
            ExpectedSuite::new(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logspam_test.cm",
                directory::Outcome::Passed,
            )
            .with_case(ExpectedTestCase::new("spam_logs", directory::Outcome::Passed))
            .with_tag(TestTag::new("internal", "true"))
            .with_matching_artifact(
                directory::ArtifactType::Syslog,
                "syslog.txt".into(),
                move |contents| {
                    // The test should produce many more lines, but logs could be dropped due to
                    // timeouts.
                    assert_geq!(contents.lines().count(), 1000);
                },
            ),
        ),
    );
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_disabled_test_exclude_disabled(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let outcome = run_test_once(
        reporter,
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm",
            ),
            None,
        )
    .await
    .expect("Running test should not fail");

    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm'
[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
[SKIPPED]	Example.Test2
[RUNNING]	Example.Test3
[SKIPPED]	Example.Test3

1 out of 1 attempted tests passed, 2 tests skipped...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm completed with result: PASSED
";
    assert_output!(output.lock().as_slice(), expected_output);

    assert_eq!(outcome, Outcome::Passed);

    directory::testing::assert_run_result(
        output_dir.path(),
        &ExpectedTestRun::new(directory::Outcome::Passed)
            .with_suite(
                ExpectedSuite::new(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm",
                    directory::Outcome::Passed
                )
                .with_tag(TestTag::new("internal", "true"))
                .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed))
                .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Skipped))
                .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Skipped))
            )
    );
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_disabled_test_include_disabled(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm",
    );
    test_params.also_run_disabled_tests = true;
    let outcome =
        run_test_once(reporter, test_params, None).await.expect("Running test should not fail");

    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm'
[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
log1 for Example.Test2
log2 for Example.Test2
log3 for Example.Test2
[PASSED]	Example.Test2
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
[FAILED]	Example.Test3

Failed tests: Example.Test3
2 out of 3 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm completed with result: FAILED
";
    assert_output!(output.lock().as_slice(), expected_output);

    assert_eq!(outcome, Outcome::Failed);

    directory::testing::assert_run_result(
        output_dir.path(),
        &ExpectedTestRun::new(directory::Outcome::Failed)
            .with_suite(
                ExpectedSuite::new(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/disabled-test-example.cm",
                    directory::Outcome::Failed
                )
                .with_tag(TestTag::new("internal", "true"))
                .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed))
                .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed))
                .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Failed))
            )
    );
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_failing_test(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let outcome = run_test_once(
            reporter,
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
            ),
            None,
        )
    .await
    .expect("Running test should not fail");

    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm'
[RUNNING]	Example.Test1
log1 for Example.Test1
log2 for Example.Test1
log3 for Example.Test1
[PASSED]	Example.Test1
[RUNNING]	Example.Test2
log1 for Example.Test2
log2 for Example.Test2
log3 for Example.Test2
[FAILED]	Example.Test2
[RUNNING]	Example.Test3
log1 for Example.Test3
log2 for Example.Test3
log3 for Example.Test3
[PASSED]	Example.Test3

Failed tests: Example.Test2
2 out of 3 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm completed with result: FAILED
";

    assert_output!(output.lock().as_slice(), expected_output);

    assert_eq!(outcome, Outcome::Failed);

    directory::testing::assert_run_result(
        output_dir.path(),
        &ExpectedTestRun::new(directory::Outcome::Failed)
            .with_suite(
                ExpectedSuite::new(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
                    directory::Outcome::Failed
                )
                .with_tag(TestTag::new("internal", "true"))
                .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed))
                .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Failed))
                .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed))
            )
    );
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_failing_v2_test_multiple_times(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                    .expect("connecting to RunBuilderProxy"),
        vec![new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
                ); 10],
                new_run_params(),
                None, reporter, futures::future::pending(),
        )
    .await;

    assert_eq!(outcome, Outcome::Failed);

    let suite_results =
        directory::TestRunResult::from_dir(output_dir.path()).expect("parse dir").suites;

    let expected_suite = ExpectedSuite::new(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
        directory::Outcome::Failed,
    )
    .with_tag(TestTag::new("internal", "true"))
    .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed))
    .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Failed))
    .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed));

    for suite_result in suite_results {
        directory::testing::assert_suite_result(output_dir.path(), &suite_result, &expected_suite);
    }
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_incomplete_test(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let outcome = run_test_once(
            reporter,
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/incomplete-test-example.cm",
            ),
            None,
        )
    .await
    .expect("Running test should not fail");

    assert_eq!(outcome, Outcome::Inconclusive);

    directory::testing::assert_run_result(
        output_dir.path(),
        &ExpectedTestRun::new(directory::Outcome::Inconclusive)
            .with_suite(
                ExpectedSuite::new(
                    "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/incomplete-test-example.cm",
                    directory::Outcome::Inconclusive
                )
                .with_tag(TestTag::new("internal", "true"))
                .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Inconclusive))
                .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed))
                .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Inconclusive))
            )
    );
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_test_invalid_test(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let outcome = run_test_once(
        reporter,
            new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/invalid-test-example.cm",
            ),
            None,
        )
    .await
    .expect("Running test should not fail");

    assert_eq!(outcome, Outcome::Inconclusive);

    directory::testing::assert_run_result(
        output_dir.path(),
        &ExpectedTestRun::new(directory::Outcome::Inconclusive)
        .with_suite(
            ExpectedSuite::new(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/invalid-test-example.cm",
                directory::Outcome::Inconclusive
            )
            .with_tag(TestTag::new("internal", "true"))
            .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Inconclusive))
            .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed))
            .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Inconclusive))
        )
    );
}

// This test also acts an example on how to right a v2 test.
// This will launch a echo_realm which will inject echo_server, launch v2 test which will
// then test that server out and return back results.
#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn launch_and_run_echo_test(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    _: tempfile::TempDir,
) {
    let outcome = run_test_once(
        reporter,
        new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm",
        ),
        None,
    )
    .await
    .expect("Running test should not fail");

    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm'
[RUNNING]	EchoTest
[PASSED]	EchoTest

1 out of 1 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm completed with result: PASSED
";
    assert_output!(output.lock().as_slice(), expected_output);

    assert_eq!(outcome, Outcome::Passed);
}

/// Time to wait for tests that verify timeout behavior.
/// If this is too short, the test may not complete launching.
const TIMEOUT_SECONDS: Option<std::num::NonZeroU32> = std::num::NonZeroU32::new(3);

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn test_timeout(reporter: TestMuxMuxReporter, output: TestOutputView, _: tempfile::TempDir) {
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
    );
    test_params.timeout_seconds = TIMEOUT_SECONDS;
    let outcome =
        run_test_once(reporter, test_params, None).await.expect("Running test should not fail");
    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm'
[RUNNING]	long_running
[TIMED_OUT]	long_running

Failed tests: long_running
0 out of 1 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm completed with result: TIMED_OUT
";
    assert_output!(output.lock().as_slice(), expected_output);
    assert_eq!(outcome, Outcome::Timedout);
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
// when a test times out, we should not run it again.
async fn test_timeout_multiple_times(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
    );
    test_params.timeout_seconds = TIMEOUT_SECONDS;
    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params; 10],
        new_run_params(),
        None,
        reporter,
        futures::future::pending(),
    )
    .await;
    assert_eq!(outcome, Outcome::Timedout);

    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm'
[RUNNING]	long_running
[TIMED_OUT]	long_running

Failed tests: long_running
0 out of 1 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm completed with result: TIMED_OUT
";
    assert_output!(output.lock().as_ref(), expected_output);

    let run_result = directory::TestRunResult::from_dir(output_dir.path()).expect("parse dir");
    assert_eq!(
        run_result.common.deref().outcome,
        directory::MaybeUnknown::Known(directory::Outcome::Timedout)
    );

    let (timed_out_suites, not_started_suites): (Vec<_>, Vec<_>) =
        run_result.suites.into_iter().partition(|suite_result| {
            suite_result.common.deref().outcome
                == directory::MaybeUnknown::Known(directory::Outcome::Timedout)
        });

    assert_eq!(timed_out_suites.len(), 1);
    directory::testing::assert_suite_result(
        output_dir.path(),
        &timed_out_suites[0],
        &ExpectedSuite::new(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
            directory::Outcome::Timedout,
        )
        .with_tag(TestTag::new("internal", "true"))
        .with_case(ExpectedTestCase::new("long_running", directory::Outcome::Timedout)),
    );

    assert_eq!(not_started_suites.len(), 9);
    for suite_result in not_started_suites {
        directory::testing::assert_suite_result(
            output_dir.path(),
            &suite_result,
            &ExpectedSuite::new(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
                directory::Outcome::NotStarted
            )
            .with_tag(TestTag::new("internal", "true"))
        );
    }
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn test_continue_on_timeout(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let mut long_test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
    );
    long_test_params.timeout_seconds = TIMEOUT_SECONDS;

    let short_test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
    );

    let mut test_params = vec![long_test_params];
    for _ in 0..10 {
        test_params.push(short_test_params.clone());
    }

    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        test_params,
        run_test_suite_lib::RunParams {
            timeout_behavior: run_test_suite_lib::TimeoutBehavior::Continue,
            stop_after_failures: None,
            experimental_parallel_execution: None,
            accumulate_debug_data: false,
            log_protocol: None,
        },
        None,
        reporter,
        futures::future::pending(),
    )
    .await;
    assert_eq!(outcome, Outcome::Timedout);

    let run_result = directory::TestRunResult::from_dir(output_dir.path()).expect("parse dir");
    assert_eq!(
        run_result.common.deref().outcome,
        directory::MaybeUnknown::Known(directory::Outcome::Timedout)
    );

    let (timed_out_suites, passing_suites): (Vec<_>, Vec<_>) =
        run_result.suites.into_iter().partition(|suite_result| {
            suite_result.common.deref().outcome
                == directory::MaybeUnknown::Known(directory::Outcome::Timedout)
        });

    assert_eq!(timed_out_suites.len(), 1);
    directory::testing::assert_suite_result(
        output_dir.path(),
        &timed_out_suites[0],
        &ExpectedSuite::new(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
            directory::Outcome::Timedout,
        )
        .with_tag(TestTag::new("internal", "true"))
        .with_case(ExpectedTestCase::new("long_running", directory::Outcome::Timedout)),
    );

    assert_eq!(passing_suites.len(), 10);
    for suite_result in passing_suites {
        directory::testing::assert_suite_result(
            output_dir.path(),
            &suite_result,
            &ExpectedSuite::new(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/passing-test-example.cm",
                directory::Outcome::Passed
            )
            .with_tag(TestTag::new("internal", "true"))
            .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed))
            .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Passed))
            .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed)),
        );
    }
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn test_stop_after_n_failures(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                    .expect("connecting to RunBuilderProxy"),
        vec![new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
                ); 10],
                run_test_suite_lib::RunParams {
                    timeout_behavior: run_test_suite_lib::TimeoutBehavior::Continue,
                    stop_after_failures: Some(5u32.try_into().unwrap()),
                    experimental_parallel_execution: None,
                    accumulate_debug_data: false,
                    log_protocol: None,
                },
                None, reporter, futures::future::pending(),
        )
    .await;
    assert_eq!(outcome, Outcome::Failed);

    let run_result = directory::TestRunResult::from_dir(output_dir.path()).expect("parse dir");
    assert_eq!(
        run_result.common.deref().outcome,
        directory::MaybeUnknown::Known(directory::Outcome::Failed)
    );

    let (failed_suites, not_started_suites): (Vec<_>, Vec<_>) =
        run_result.suites.into_iter().partition(|suite_result| {
            suite_result.common.deref().outcome
                == directory::MaybeUnknown::Known(directory::Outcome::Failed)
        });

    assert_eq!(failed_suites.len(), 5);
    assert_eq!(not_started_suites.len(), 5);
    for failed_suite in failed_suites {
        directory::testing::assert_suite_result(
            output_dir.path(),
            &failed_suite,
            &ExpectedSuite::new(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
                directory::Outcome::Failed
            )
            .with_tag(TestTag::new("internal", "true"))
            .with_case(ExpectedTestCase::new("Example.Test1", directory::Outcome::Passed))
            .with_case(ExpectedTestCase::new("Example.Test2", directory::Outcome::Failed))
            .with_case(ExpectedTestCase::new("Example.Test3", directory::Outcome::Passed))
        );
    }

    for not_started_suite in not_started_suites {
        directory::testing::assert_suite_result(
            output_dir.path(),
            &not_started_suite,
            &ExpectedSuite::new(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/failing-test-example.cm",
                directory::Outcome::NotStarted
            )
            .with_tag(TestTag::new("internal", "true"))
        );
    }
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn test_passes_with_large_timeout(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    _: tempfile::TempDir,
) {
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm",
    );
    test_params.timeout_seconds = std::num::NonZeroU32::new(600);
    let outcome =
        run_test_once(reporter, test_params, None).await.expect("Running test should not fail");

    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm'
[RUNNING]	EchoTest
[PASSED]	EchoTest

1 out of 1 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/echo_test_realm.cm completed with result: PASSED
";
    assert_output!(output.lock().as_slice(), expected_output);

    assert_eq!(outcome, Outcome::Passed);
}

#[test_case("batch", LogsIteratorOption::BatchIterator ; "batch")]
#[test_case("archive", LogsIteratorOption::ArchiveIterator ; "archive")]
#[fuchsia::test]
async fn test_logging_component(subcase: &'static str, iterator_option: LogsIteratorOption) {
    run_with_reporter(
        &format!("test_logging_component_{}", subcase), 
        |reporter: TestMuxMuxReporter, output: TestOutputView, output_dir: tempfile::TempDir| async move {
            let mut test_params = new_test_params(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logging_test.cm",
            );
            test_params.timeout_seconds = std::num::NonZeroU32::new(600);
            let mut run_params = new_run_params();
            run_params.log_protocol = Some(iterator_option);
            let outcome = run_test_suite_lib::run_tests_and_get_outcome(
                fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                    .expect("connecting to RunBuilderProxy"),
                vec![test_params],
                run_params,
                None,
                run_test_suite_lib::output::RunReporter::new(reporter),
                futures::future::pending(),
            )
            .await;

            let expected_logs =
                "[TIMESTAMP][PID][TID][<root>][log_and_exit,logging_test] DEBUG: Logging initialized
[TIMESTAMP][PID][TID][<root>][log_and_exit,logging_test] DEBUG: my debug message
[TIMESTAMP][PID][TID][<root>][log_and_exit,logging_test] INFO: my info message
[TIMESTAMP][PID][TID][<root>][log_and_exit,logging_test] WARN: my warn message
";

            let expected_output = format!("Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logging_test.cm'
[RUNNING]	log_and_exit
{}[PASSED]	log_and_exit

1 out of 1 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logging_test.cm completed with result: PASSED
", expected_logs);
            assert_output!(output.lock().as_slice(), expected_output.as_str());
            assert_eq!(outcome, Outcome::Passed);

            directory::testing::assert_run_result(
                output_dir.path(),
                &ExpectedTestRun::new(directory::Outcome::Passed).with_suite(
                    ExpectedSuite::new(
                        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logging_test.cm",
                        directory::Outcome::Passed,
                    )
                    .with_tag(TestTag::new("internal", "true"))
                    .with_matching_artifact(
                        directory::ArtifactType::Syslog,
                        "syslog.txt".into(),
                        move |contents| {
                            assert_output!(contents.as_bytes(), expected_logs);
                        },
                    )
                    .with_case(ExpectedTestCase::new("log_and_exit", directory::Outcome::Passed)),
                ),
            );
        }
    ).await;
}

#[test_case("batch", LogsIteratorOption::BatchIterator ; "batch")]
#[test_case("archive", LogsIteratorOption::ArchiveIterator ; "archive")]
#[fuchsia::test]
async fn test_logging_component_min_severity(
    subcase: &'static str,
    iterator_option: LogsIteratorOption,
) {
    run_with_reporter(
        &format!("test_logging_component_{}", subcase), 
        |reporter: TestMuxMuxReporter, output: TestOutputView, _output_dir: tempfile::TempDir| async move {

        let mut test_params = new_test_params(
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logging_test.cm",
        );
        test_params.timeout_seconds = std::num::NonZeroU32::new(600);
        let mut run_params = new_run_params();
        run_params.log_protocol = Some(iterator_option);
        let outcome = run_test_suite_lib::run_tests_and_get_outcome(
            fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
                .expect("connecting to RunBuilderProxy"),
            vec![test_params],
            run_params,
            Some(Severity::Info),
            run_test_suite_lib::output::RunReporter::new(reporter),
            futures::future::pending(),
        )
        .await;

        let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logging_test.cm'
[RUNNING]	log_and_exit
[TIMESTAMP][PID][TID][<root>][log_and_exit,logging_test] INFO: my info message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,logging_test] WARN: my warn message\n\
[PASSED]	log_and_exit

1 out of 1 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/logging_test.cm completed with result: PASSED
";
        assert_output!(output.lock().as_slice(), expected_output);
        assert_eq!(outcome, Outcome::Passed);
    }).await;
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn test_stdout_and_log_ansi(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    _: tempfile::TempDir,
) {
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
    );
    test_params.timeout_seconds = std::num::NonZeroU32::new(600);
    let outcome = run_test_once(reporter, test_params, Some(Severity::Info))
        .await
        .expect("Running test should not fail");

    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm'
[RUNNING]	log_ansi_test
[TIMESTAMP][PID][TID][<root>][log_ansi_test] INFO: \u{1b}[31mred log\u{1b}[0m
[PASSED]	log_ansi_test
[RUNNING]	stdout_ansi_test
\u{1b}[31mred stdout\u{1b}[0m
[PASSED]	stdout_ansi_test

2 out of 2 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm completed with result: PASSED
";
    assert_output!(output.lock().as_slice(), expected_output);
    assert_eq!(outcome, Outcome::Passed);
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn test_stdout_and_log_filter_ansi(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    _: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new_ansi_filtered(reporter);
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
    );
    test_params.timeout_seconds = std::num::NonZeroU32::new(600);

    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params],
        new_run_params(),
        Some(Severity::Info),
        reporter,
        futures::future::pending(),
    )
    .await;

    let expected_output = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm'
[RUNNING]	log_ansi_test
[TIMESTAMP][PID][TID][<root>][log_ansi_test] INFO: red log
[PASSED]	log_ansi_test
[RUNNING]	stdout_ansi_test
red stdout
[PASSED]	stdout_ansi_test

2 out of 2 tests passed...
fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm completed with result: PASSED
";
    assert_output!(output.lock().as_ref(), expected_output);
    assert_eq!(outcome, Outcome::Passed);
}

#[test_case(LogsIteratorOption::BatchIterator ; "batch")]
#[test_case(LogsIteratorOption::ArchiveIterator ; "archive")]
#[fuchsia::test]
async fn test_logging_component_max_severity_info(iterator_option: LogsIteratorOption) {
    test_max_severity(Severity::Info, iterator_option).await;
}

#[test_case(LogsIteratorOption::BatchIterator ; "batch")]
#[test_case(LogsIteratorOption::ArchiveIterator ; "archive")]
#[fuchsia::test]
async fn test_logging_component_max_severity_warn(iterator_option: LogsIteratorOption) {
    test_max_severity(Severity::Warn, iterator_option).await;
}

#[test_case(LogsIteratorOption::BatchIterator ; "batch")]
#[test_case(LogsIteratorOption::ArchiveIterator ; "archive")]
#[fuchsia::test]
async fn test_logging_component_max_severity_error(iterator_option: LogsIteratorOption) {
    test_max_severity(Severity::Error, iterator_option).await;
}

async fn test_max_severity(max_severity: Severity, iterator_option: LogsIteratorOption) {
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm",
    );
    test_params.timeout_seconds = std::num::NonZeroU32::new(600);
    test_params.max_severity_logs = Some(max_severity);
    let (reporter, output) = create_shell_reporter();
    let mut run_params = new_run_params();
    run_params.log_protocol = Some(iterator_option);
    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params],
        run_params,
        None,
        run_test_suite_lib::output::RunReporter::new(reporter),
        futures::future::pending(),
    )
    .await;

    let expected_output_prefix = "Running test 'fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm'
[RUNNING]	log_and_exit
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] DEBUG: Logging initialized
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] INFO: my info message
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] WARN: my warn message
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] ERROR: [../../src/sys/run_test_suite/tests/test_data/error_logging_test.rs(12)] my error message
[PASSED]	log_and_exit

1 out of 1 tests passed...

";
    let (expected_outcome, expected_output_postfix) = match max_severity {
        Severity::Info => (
                Outcome::Failed,
                "Test fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm produced unexpected high-severity logs:
----------------xxxxx----------------
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] WARN: my warn message
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] ERROR: [../../src/sys/run_test_suite/tests/test_data/error_logging_test.rs(12)] my error message

----------------xxxxx----------------
Failing this test. See: https://fuchsia.dev/fuchsia-src/development/diagnostics/test_and_logs#restricting_log_severity

fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm completed with result: FAILED
"
        ),
        Severity::Warn => (
            Outcome::Failed,
            "Test fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm produced unexpected high-severity logs:
----------------xxxxx----------------
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] ERROR: [../../src/sys/run_test_suite/tests/test_data/error_logging_test.rs(12)] my error message

----------------xxxxx----------------
Failing this test. See: https://fuchsia.dev/fuchsia-src/development/diagnostics/test_and_logs#restricting_log_severity

fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm completed with result: FAILED
"
        ),
        Severity::Error => (
            Outcome::Passed,
            "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm completed with result: PASSED"
        ),
        _ => panic!("unexpected severity")
    };
    let expected_output = format!("{}{}", expected_output_prefix, expected_output_postfix);
    assert_output!(output.lock().as_slice(), expected_output);
    assert_eq!(outcome, expected_outcome);
}

#[fuchsia::test]
async fn test_does_not_resolve() {
    let test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/nonexistant_test.cm",
    );
    let log_opts = None;
    let outcome = run_test_once(output::NoopReporter, test_params, log_opts).await.unwrap();
    let origin_error = match outcome {
        Outcome::Error { origin } => origin,
        other => panic!("Expected an error outcome but got {:?}", other),
    };
    assert!(!origin_error.is_internal_error());
    assert_matches!(
        Arc::try_unwrap(origin_error).unwrap(),
        RunTestSuiteError::Launch(LaunchError::InstanceCannotResolve)
    );
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn test_stdout_to_directory(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
    );
    test_params.timeout_seconds = std::num::NonZeroU32::new(600);

    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params],
        new_run_params(),
        None,
        reporter,
        futures::future::pending(),
    )
    .await;

    assert_eq!(outcome, Outcome::Passed);

    let expected_test_run = ExpectedTestRun::new(directory::Outcome::Passed)
        .with_any_start_time()
        .with_no_run_duration()
        .with_suite(
            ExpectedSuite::new(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/stdout_ansi_test.cm",
                directory::Outcome::Passed,
            )
            .with_tag(TestTag::new("internal", "true"))
            .with_matching_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), |logs| {
                assert_output!(
                    logs.as_bytes(),
                    "\n[TIMESTAMP][PID][TID][<root>][log_ansi_test] INFO: \u{1b}[31mred log\u{1b}[0m"
                );
            })
            .with_case(
                ExpectedTestCase::new("stdout_ansi_test", directory::Outcome::Passed)
                    .with_artifact(
                        directory::ArtifactType::Stdout,
                        "stdout.txt".into(),
                        "\u{1b}[31mred stdout\u{1b}[0m\n",
                    )
                    .with_no_run_duration()
                    .with_any_start_time(),
            )
            .with_case(
                ExpectedTestCase::new("log_ansi_test", directory::Outcome::Passed)
                    .with_no_run_duration()
                    .with_any_start_time(),
            )
            .with_any_run_duration()
            .with_any_start_time()
        );
    directory::testing::assert_run_result(output_dir.path(), &expected_test_run);
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn test_syslog_to_directory(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let mut test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm",
    );
    test_params.timeout_seconds = std::num::NonZeroU32::new(600);
    test_params.max_severity_logs = Some(Severity::Warn);

    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params],
        new_run_params(),
        None,
        reporter,
        futures::future::pending(),
    )
    .await;

    assert_eq!(outcome, Outcome::Failed);

    const EXPECTED_SYSLOG: &str =  "[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] DEBUG: Logging initialized\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] INFO: my info message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] WARN: my warn message\n\
[TIMESTAMP][PID][TID][<root>][log_and_exit,error_logging_test] ERROR: [../../src/sys/run_test_suite/tests/test_data/error_logging_test.rs(12)] my error message\n\
";
    let expected_test_run = ExpectedTestRun::new(directory::Outcome::Failed)
        .with_suite(
            ExpectedSuite::new(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/error_logging_test.cm",
                directory::Outcome::Failed,
            )
            .with_tag(TestTag::new("internal", "true"))
            .with_case(
                ExpectedTestCase::new("log_and_exit", directory::Outcome::Passed)
                    .with_any_start_time()
                    .with_no_run_duration(),
            )
            .with_matching_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), |actual| {
                assert_output!(actual.as_bytes(), EXPECTED_SYSLOG);
            })
            .with_matching_artifact(directory::ArtifactType::RestrictedLog, "restricted_logs.txt".into(), |actual| {
                assert!(actual.contains("ERROR: [../../src/sys/run_test_suite/tests/test_data/error_logging_test.rs(12)] my error message"))
            })
            .with_any_start_time()
            .with_any_run_duration()
        );

    directory::testing::assert_run_result(output_dir.path(), &expected_test_run);
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn test_custom_artifacts_to_directory(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/custom_artifact_user.cm",
    );

    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params],
        new_run_params(),
        None,
        reporter,
        futures::future::pending(),
    )
    .await;

    assert_eq!(outcome, Outcome::Passed);

    let expected_test_run = ExpectedTestRun::new(directory::Outcome::Passed)
        .with_suite(
            ExpectedSuite::new(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/custom_artifact_user.cm",
                directory::Outcome::Passed,
            )
            .with_tag(TestTag::new("internal", "true"))
            .with_case(
                ExpectedTestCase::new("use_artifact", directory::Outcome::Passed)
                    .with_any_start_time()
                    .with_no_run_duration(),
            )
            .with_artifact(directory::ArtifactType::Syslog, "syslog.txt".into(), "")
            .with_directory_artifact(
                directory::ArtifactMetadata {
                    artifact_type: directory::ArtifactType::Custom.into(),
                    component_moniker: Some(".".to_string()),
                },
                Option::<&str>::None,
                ExpectedDirectory::new().with_file("artifact.txt", "Hello, world!"),
            )
            .with_any_start_time()
            .with_any_run_duration()
        );

    directory::testing::assert_run_result(output_dir.path(), &expected_test_run);
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn test_terminate_signal(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
    );

    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params],
        new_run_params(),
        None,
        reporter,
        futures::future::ready(()),
    )
    .await;

    assert_eq!(outcome, Outcome::Cancelled);

    let run_result = directory::TestRunResult::from_dir(output_dir.path()).expect("parse dir");
    assert_eq!(
        run_result.common.deref().outcome,
        directory::MaybeUnknown::Known(directory::Outcome::Inconclusive)
    );

    // Based on the exact timing, it's possible some test cases were reported, or the suite never
    // started, so manually assert only on the fields that shouldn't vary.
    assert_eq!(run_result.suites.len(), 1);
    let directory::SuiteResult { common, .. } = run_result.suites.into_iter().next().unwrap();
    assert_matches!(
        common.deref().outcome,
        directory::MaybeUnknown::Known(directory::Outcome::Inconclusive)
            | directory::MaybeUnknown::Known(directory::Outcome::NotStarted)
    );
    assert_eq!(
        common.deref().name,
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm"
    );

    // TODO(satsukiu): add a `Reporter` implementation for tests that signals on events and use it
    // to make more sophisticated tests. Since we need to make assertions on the directory reporter
    // simultaneously, we'll need to support writing to multiple reporters at once as well.
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn test_terminate_signal_multiple_suites(
    reporter: TestMuxMuxReporter,
    _: TestOutputView,
    output_dir: tempfile::TempDir,
) {
    let reporter = output::RunReporter::new(reporter);
    let test_params = new_test_params(
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
    );

    let outcome = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![test_params; 10],
        new_run_params(),
        None,
        reporter,
        futures::future::ready(()),
    )
    .await;

    assert_eq!(outcome, Outcome::Cancelled);

    let run_result = directory::TestRunResult::from_dir(output_dir.path()).expect("parse dir");
    assert_eq!(
        run_result.common.deref().outcome,
        directory::MaybeUnknown::Known(directory::Outcome::Inconclusive)
    );

    // There should be at most one test that started and is inconclusive. Remaining tests should
    // be NOT_STARTED. Note this assumes that test manager is running tests sequentially, this test
    // could start flaking when this changes.
    let (inconclusive_suite_results, other_suite_results): (Vec<_>, Vec<_>) =
        run_result.suites.into_iter().partition(|suite| {
            suite.common.deref().outcome
                == directory::MaybeUnknown::Known(directory::Outcome::Inconclusive)
        });
    assert!(inconclusive_suite_results.len() <= 1);
    assert!(other_suite_results.len() >= 9);

    other_suite_results.into_iter().for_each(|suite_result| {
        directory::testing::assert_suite_result(
            output_dir.path(),
            &suite_result,
            &ExpectedSuite::new(
                "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/long_running_test.cm",
                directory::Outcome::NotStarted,
            )
            .with_tag(TestTag::new("internal", "true")),
        );
    });
}

async fn wait_until_contains(output: &TestOutputView, contents: &str) {
    loop {
        if String::from_utf8(output.lock().clone()).unwrap().contains(contents) {
            break;
        }
        fuchsia_async::Timer::new(std::time::Duration::from_millis(250)).await;
    }
}

#[fixture::fixture(run_with_reporter)]
#[fuchsia::test]
async fn test_collect_stream_artifacts_from_hung_test(
    reporter: TestMuxMuxReporter,
    output: TestOutputView,
    _: tempfile::TempDir,
) {
    // This test verifies that artifacts that need to be shown to a developer in realtime are
    // streamed, even if the test hangs. For example, this test would fail submission of a change
    // that made log collection occur once, after the suite completed.
    const SUITE_NAME: &'static str =
        "fuchsia-pkg://fuchsia.com/run_test_suite_integration_tests#meta/log_then_hang_test.cm";
    let reporter = output::RunReporter::new(reporter);
    let cancel_event = async_utils::event::Event::new();
    let cancel_waiter = cancel_event.wait();

    let run_fut = run_test_suite_lib::run_tests_and_get_outcome(
        fuchsia_component::client::connect_to_protocol::<RunBuilderMarker>()
            .expect("connecting to RunBuilderProxy"),
        vec![new_test_params(SUITE_NAME)],
        new_run_params(),
        None,
        reporter,
        cancel_waiter,
    );
    let observer_fut = async move {
        // check that test started, and we can observe streamed artifacts before run completes
        futures::future::join4(
            wait_until_contains(&output, "[RUNNING]\tlog_then_hang"),
            wait_until_contains(&output, "stdout from hanging test"),
            wait_until_contains(&output, "stderr from hanging test"),
            wait_until_contains(&output, "syslog from hanging test"),
        )
        .await;

        // Verify that the test hasn't finished.
        let contents = String::from_utf8(output.lock().clone()).unwrap();
        assert!(!contents.contains("cancelled before completion"));

        // Cancel the test, then verify test stops
        cancel_event.signal();
        wait_until_contains(&output, "cancelled before completion").await;
        let contents = String::from_utf8(output.lock().clone()).unwrap();
        assert!(contents.contains("cancelled before completion"));
    };
    let (outcome, ()) = futures::future::join(run_fut, observer_fut).await;
    assert_matches!(outcome, Outcome::Cancelled);
}
