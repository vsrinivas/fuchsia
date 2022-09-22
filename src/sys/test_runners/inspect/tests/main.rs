// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    diagnostics_data::{hierarchy, Data, DiagnosticsHierarchy, Property},
    fake_archive_accessor::FakeArchiveAccessor,
    fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::{CaseStatus, RunOptions, SuiteStatus},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_component_test::{Capability, ChildOptions, RealmBuilder, Ref, Route},
    futures::prelude::*,
    paste::paste,
    pretty_assertions::assert_eq,
    serde_json,
    std::collections::BTreeSet,
    test_manager_test_lib::{GroupRunEventByTestCase, RunEvent},
};

#[derive(Debug)]
struct IntegrationCaseStatus {
    events: Vec<RunEvent>,
    selectors_requested: Vec<BTreeSet<String>>,
}

async fn run_test(
    test_url: &str,
    fake_archive_output: Vec<String>,
    archive_service_name: &'static str,
) -> Result<IntegrationCaseStatus, Error> {
    let builder = RealmBuilder::new().await.expect("create realm builder");

    let fake = FakeArchiveAccessor::new(&fake_archive_output, None);
    let fake_clone = fake.clone();
    let test_manager = builder
        .add_child(
            "test_manager",
            "fuchsia-pkg://fuchsia.com/inspect-runner-integration-test#meta/test_manager_for_tests.cm",
            ChildOptions::new(),
        )
        .await?;
    let fake_archivist = builder
        .add_local_child(
            "fake_archivist",
            move |handles| {
                let fake = fake_clone.clone();
                async move {
                    let _ = &handles;
                    let mut fs = ServiceFs::new();
                    fs.dir("svc").add_fidl_service_at(archive_service_name, move |req| {
                        let fake = fake.clone();
                        fuchsia_async::Task::spawn(async move {
                            fake.serve_stream(req)
                                .await
                                .map_err(|e| println!("Fake stream had error {}", e))
                                .ok();
                        })
                        .detach();
                    });
                    fs.serve_connection(handles.outgoing_dir).expect("serve fake archivist");
                    fs.collect::<()>().await;
                    Ok(())
                }
                .boxed()
            },
            ChildOptions::new(),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                .capability(Capability::protocol_by_name("fuchsia.sys2.EventSource"))
                .capability(Capability::protocol_by_name("fuchsia.component.resolution.Resolver"))
                .capability(Capability::storage("tmp"))
                .capability(Capability::event_stream("started_v2"))
                .capability(Capability::event_stream("running_v2"))
                .capability(Capability::event_stream("stopped_v2"))
                .capability(Capability::event_stream("destroyed_v2"))
                .capability(Capability::event_stream("capability_requested_v2"))
                .capability(Capability::event_stream("directory_ready_v2"))
                .from(Ref::parent())
                .to(&test_manager),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol_by_name("fuchsia.diagnostics.ArchiveAccessor"))
                .capability(Capability::protocol_by_name(
                    "fuchsia.diagnostics.FeedbackArchiveAccessor",
                ))
                .capability(Capability::protocol_by_name(
                    "fuchsia.diagnostics.LegacyMetricsArchiveAccessor",
                ))
                .from(&fake_archivist)
                .to(&test_manager),
        )
        .await?;
    builder
        .add_route(
            Route::new()
                .capability(Capability::protocol::<ftest_manager::RunBuilderMarker>())
                .from(&test_manager)
                .to(Ref::parent()),
        )
        .await?;

    let instance = builder.build().await?;

    let run_builder =
        instance.root.connect_to_protocol_at_exposed_dir::<ftest_manager::RunBuilderMarker>()?;
    let run_builder = test_manager_test_lib::TestBuilder::new(run_builder);
    let suite_instance = run_builder
        .add_suite(
            test_url,
            RunOptions { run_disabled_tests: Some(false), parallel: Some(1), ..RunOptions::EMPTY },
        )
        .await
        .context("Cannot create suite instance")?;
    let builder_run = fasync::Task::spawn(async move { run_builder.run().await });
    let (events, _logs) = test_runners_test_lib::process_events(suite_instance, false).await?;
    builder_run.await.context("builder execution failed")?;

    Ok(IntegrationCaseStatus {
        events: events,
        selectors_requested: fake.get_selectors_requested(),
    })
}

fn filter_out_println(event: RunEvent) -> Option<RunEvent> {
    match event {
        RunEvent::CaseStdout { name, stdout_message } => {
            println!("Test stdout [{}]: {}", name, stdout_message);
            None
        }
        e => Some(e),
    }
}

#[fuchsia::test]
async fn launch_and_test_sample_test() {
    let test_url =
        "fuchsia-pkg://fuchsia.com/inspect-runner-integration-test#meta/sample_inspect_tests.cm";

    let fake_data = vec![
        Data::for_inspect(
            "bootstrap/archivist",
            Some(hierarchy! {
                root: {
                    version: "1.0",
                }
            }),
            0,
            "no-url",
            "fake-file-name",
            vec![],
        ),
        Data::for_inspect(
            "bootstrap/archivist",
            Some(hierarchy! {
                root: {
                    event_stats: {
                        recent_events: {
                            "0": {
                                event: "START",
                            }
                        }
                    }
                }
            }),
            0,
            "no-url",
            "fake-file-name",
            vec![],
        ),
        // Inject one that is missing data to ensure we retry correctly.
        Data::for_inspect(
            "bootstrap/archivist",
            Some(hierarchy! {root: {}}),
            0,
            "no-url",
            "fake-file-name",
            vec![],
        ),
        Data::for_inspect(
            "bootstrap/archivist",
            Some(hierarchy! { root: {
                event_stats: {
                    components_seen_running: 2i64,
                }
            }}),
            0,
            "no-url",
            "fake-file-name",
            vec![],
        ),
    ]
    .into_iter()
    .map(|d| serde_json::to_string_pretty(&d))
    .collect::<Result<Vec<_>, _>>()
    .expect("format fake data");

    let IntegrationCaseStatus { events, selectors_requested } =
        run_test(test_url, fake_data, "fuchsia.diagnostics.ArchiveAccessor").await.unwrap();

    assert_eq!(
        vec![
            vec!["bootstrap/archivist:root"],
            vec!["bootstrap/archivist:root/event_stats/recent_events/*:event"],
            vec!["bootstrap/archivist:root/event_stats:components_seen_running"],
            vec!["bootstrap/archivist:root/event_stats:components_seen_running"],
        ],
        selectors_requested
            .into_iter()
            .map(|set| set.into_iter().collect::<Vec<String>>())
            .collect::<Vec<Vec<String>>>()
    );

    let mut expected_events = vec![RunEvent::suite_started()];
    expected_events.extend(
        vec![
            "bootstrap/archivist:root",
            "bootstrap/archivist:root/event_stats/recent_events/*:event WHERE [a] Count(Filter(Fn([b], b == 'START'), a)) > 0",
            "bootstrap/archivist:root/event_stats:components_seen_running WHERE [a] a > 1"
        ]
            .into_iter()
            .map(|case_name| vec![
                RunEvent::case_found(case_name),RunEvent::case_started(case_name),
                RunEvent::case_stopped(case_name, CaseStatus::Passed),RunEvent::case_finished(case_name)
            ])
        .flatten()
    );
    expected_events.push(RunEvent::suite_stopped(SuiteStatus::Passed));

    // Compare events, ignoring stdout messages.
    assert_eq!(
        expected_events.into_iter().group_by_test_case_unordered(),
        events.into_iter().filter_map(filter_out_println).group_by_test_case_unordered()
    );
}

/// Options to construct example data.
struct ExampleDataOpts {
    /// If set, publish the given value as "version". Otherwise omit it.
    version: Option<&'static str>,
    /// If set, publish the given value as "value". Otherwise omit it.
    value: Option<u64>,
}

// Create a hierarchy with optional values of the following structure:
//
// root:
//   version: <version>
//   value: <value>
//
// The example tests expect version to be present and value to be in range [5, 10).
fn create_example_data(opts: ExampleDataOpts) -> Data<diagnostics_data::Inspect> {
    // Create the list of properties, leaving out those that were not set.
    let properties = vec![
        opts.version.as_ref().map(|v| Property::String("version".to_string(), v.to_string())),
        opts.value.as_ref().map(|v| Property::Uint("value".to_string(), *v)),
    ]
    .into_iter()
    .filter_map(|v| v)
    .collect();
    Data::for_inspect(
        "example",
        Some(DiagnosticsHierarchy::new("root", properties, vec![])),
        0,
        "no-url",
        "fake-file-name",
        vec![],
    )
}

async fn example_test_success(test_url: &'static str, accessor_service: &'static str) {
    let fake_data = vec![
        create_example_data(ExampleDataOpts { version: None, value: Some(5) }),
        create_example_data(ExampleDataOpts { version: Some("1.0"), value: None }),
    ]
    .into_iter()
    .map(|d| serde_json::to_string_pretty(&d))
    .collect::<Result<Vec<_>, _>>()
    .expect("format fake data");

    let IntegrationCaseStatus { events, selectors_requested } =
        run_test(test_url, fake_data, accessor_service).await.unwrap();

    assert_eq!(
        vec![vec!["example:root:value"], vec!["example:root:version"],],
        selectors_requested
            .into_iter()
            .map(|set| set.into_iter().collect::<Vec<String>>())
            .collect::<Vec<Vec<String>>>()
    );

    let mut expected_events = vec![RunEvent::suite_started()];

    expected_events.extend(
        vec!["example:root:value WHERE [a] And(a >= 5, a < 10)", "example:root:version"]
            .into_iter()
            .map(|case_name| {
                vec![
                    RunEvent::case_found(case_name),
                    RunEvent::case_started(case_name),
                    RunEvent::case_stopped(case_name, CaseStatus::Passed),
                    RunEvent::case_finished(case_name),
                ]
            })
            .flatten(),
    );
    expected_events.push(RunEvent::suite_stopped(SuiteStatus::Passed));

    // Compare events, ignoring stdout messages.
    assert_eq!(
        expected_events.into_iter().group_by_test_case_unordered(),
        events.into_iter().filter_map(filter_out_println).group_by_test_case_unordered()
    );
}

#[derive(Clone, Copy, PartialEq)]
enum FailureMode {
    // Don't artificially create a failure, depend on a failure unrelated to values.
    NoValueFailure,
    // Set value to be too small
    ValueTooSmall,
    // Set value to be too large
    ValueTooLarge,
    // Do not set value at all
    MissingValue,
    // Do not set version at all
    MissingVersion,
}

async fn example_test_failure(
    test_url: &'static str,
    accessor_service: &'static str,
    failure_mode: FailureMode,
) {
    let (fake_data, expected_results) = match failure_mode {
        FailureMode::NoValueFailure => (
            vec![create_example_data(ExampleDataOpts { version: Some("1.0"), value: Some(5) })],
            vec![CaseStatus::Failed, CaseStatus::Failed],
        ),
        FailureMode::ValueTooSmall => (
            vec![create_example_data(ExampleDataOpts { version: Some("1.0"), value: Some(4) })],
            vec![CaseStatus::Failed, CaseStatus::Passed],
        ),
        FailureMode::ValueTooLarge => (
            vec![create_example_data(ExampleDataOpts { version: Some("1.0"), value: Some(10) })],
            vec![CaseStatus::Failed, CaseStatus::Passed],
        ),
        FailureMode::MissingValue => (
            vec![create_example_data(ExampleDataOpts { version: Some("1.0"), value: None })],
            vec![CaseStatus::Failed, CaseStatus::Passed],
        ),
        FailureMode::MissingVersion => (
            vec![create_example_data(ExampleDataOpts { version: None, value: Some(5) })],
            vec![CaseStatus::Passed, CaseStatus::Failed],
        ),
    };

    let fake_data = fake_data
        .into_iter()
        .cycle()
        .take(2000) // Create a repeat of the value so that repeated reads keep finding the same values.
        .map(|d| serde_json::to_string_pretty(&d))
        .collect::<Result<Vec<_>, _>>()
        .expect("format fake data");

    let IntegrationCaseStatus { events, selectors_requested } =
        run_test(test_url, fake_data, accessor_service).await.unwrap();

    let selectors_requested = selectors_requested.into_iter().flatten().collect::<BTreeSet<_>>();
    if failure_mode == FailureMode::NoValueFailure {
        // The only way the tests failed is if no requests succeeded.
        assert_eq!(BTreeSet::new(), selectors_requested);
    } else {
        assert_eq!(
            vec!["example:root:value", "example:root:version"]
                .into_iter()
                .map(str::to_string)
                .collect::<BTreeSet<_>>(),
            selectors_requested
        );
    }

    let mut expected_events: Vec<RunEvent> = vec![RunEvent::suite_started()];
    expected_events.extend(
        vec!["example:root:value WHERE [a] And(a >= 5, a < 10)", "example:root:version"]
            .into_iter()
            .zip(expected_results.into_iter())
            .map(|(case_name, result)| {
                vec![
                    RunEvent::case_found(case_name),
                    RunEvent::case_started(case_name),
                    RunEvent::case_stopped(case_name, result),
                    RunEvent::case_finished(case_name),
                ]
            })
            .flatten(),
    );
    expected_events.push(RunEvent::suite_stopped(SuiteStatus::Failed));

    // Compare events, ignoring stdout messages.
    assert_eq!(
        expected_events.into_iter().group_by_test_case_unordered(),
        events.into_iter().filter_map(filter_out_println).group_by_test_case_unordered()
    );
}

macro_rules! make_tests {
    ($name:ident, $pkg:expr, $correct_accessor:expr, $wrong_accessor:expr) => {
        paste! {
            #[fuchsia::test]
            async fn [<$name _success>]() {
                example_test_success($pkg, $correct_accessor).await;
            }

            #[fuchsia::test]
            async fn [<$name _failure_wrong_accessor>]() {
                example_test_failure($pkg, $wrong_accessor, FailureMode::NoValueFailure).await;
            }

            #[fuchsia::test]
            async fn [<$name _failure_value_too_small>]() {
                example_test_failure($pkg, $correct_accessor, FailureMode::ValueTooSmall).await;
            }

            #[fuchsia::test]
            async fn [<$name _failure_value_too_large>]() {
                example_test_failure($pkg, $correct_accessor, FailureMode::ValueTooLarge).await;
            }

            #[fuchsia::test]
            async fn [<$name _failure_value_missing>]() {
                example_test_failure($pkg, $correct_accessor, FailureMode::MissingValue).await;
            }

            #[fuchsia::test]
            async fn [<$name _failure_version_missing>]() {
                example_test_failure($pkg, $correct_accessor, FailureMode::MissingVersion).await;
            }
        }
    };
}

make_tests!(
    archive_example,
    "fuchsia-pkg://fuchsia.com/inspect-runner-integration-test#meta/archive_example.cm",
    "fuchsia.diagnostics.ArchiveAccessor",
    "fuchsia.diagnostics.FeedbackArchiveAccessor"
);
make_tests!(
    feedback_example,
    "fuchsia-pkg://fuchsia.com/inspect-runner-integration-test#meta/feedback_example.cm",
    "fuchsia.diagnostics.FeedbackArchiveAccessor",
    "fuchsia.diagnostics.ArchiveAccessor"
);
make_tests!(
    legacy_example,
    "fuchsia-pkg://fuchsia.com/inspect-runner-integration-test#meta/legacy_example.cm",
    "fuchsia.diagnostics.LegacyMetricsArchiveAccessor",
    "fuchsia.diagnostics.ArchiveAccessor"
);

async fn test_failure_case(url: &str) {
    let output = run_test(url, vec![], "fuchsia.diagnostics.ArchiveAccessor").await;
    println!("Output was {:?}", output);
    assert!(output.is_err());
}

macro_rules! make_failure_test {
    ($name: ident, $url: expr) => {
        paste! {
            #[fuchsia::test]
            async fn [<$name _failure>]() {
                test_failure_case($url).await;
            }
        }
    };
}

make_failure_test!(
    invalid_case,
    "fuchsia-pkg://fuchsia.com/inspect-runner-integration-test#meta/invalid_case.cm"
);

make_failure_test!(
    invalid_evaluation,
    "fuchsia-pkg://fuchsia.com/inspect-runner-integration-test#meta/invalid_evaluation.cm"
);

make_failure_test!(
    missing_program,
    "fuchsia-pkg://fuchsia.com/inspect-runner-integration-test#meta/missing_program.cm"
);

make_failure_test!(
    unknown_pipeline,
    "fuchsia-pkg://fuchsia.com/inspect-runner-integration-test#meta/unknown_pipeline.cm"
);
