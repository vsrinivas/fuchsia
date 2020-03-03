// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test as ftest,
    fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::Result_,
    fuchsia_async as fasync,
    fuchsia_component::client,
    futures::io::AsyncWrite,
    futures::{channel::mpsc, prelude::*},
    std::collections::HashSet,
    test_executor::TestEvent,
    uuid::Uuid,
};

// Start test manager and serve it over `stream`.
pub async fn run_test_manager(
    mut stream: ftest_manager::HarnessRequestStream,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            ftest_manager::HarnessRequest::RunSuite { suite_url, logger, responder } => {
                let mut logger = fasync::Socket::from_socket(logger)?;

                let result = run_test(suite_url.to_string(), &mut logger).await;
                if let Err(err) = result {
                    logger
                        .write(
                            format!(
                                "Test manager encountered error trying to run tests: {:?}\n",
                                err
                            )
                            .as_bytes(),
                        )
                        .await
                        .context("Can't write output")?;
                    responder.send(Result_::Error).context("Can't send result back")?;
                    continue;
                }

                let (result, executed, passed, successful_completion) = result.unwrap();

                logger
                    .write(
                        format!("\n{} out of {} tests passed...\n", passed.len(), executed.len())
                            .as_bytes(),
                    )
                    .await
                    .context("Can't write output")?;
                logger
                    .write(
                        format!("{} completed with result: {:?}\n", suite_url, result).as_bytes(),
                    )
                    .await
                    .context("Can't write output")?;

                if !successful_completion {
                    logger
                        .write(format!("{} did not complete successfully\n", suite_url).as_bytes())
                        .await
                        .context("Can't write output")?;
                }

                responder.send(result).context("Can't send result back")?;
            }
        }
    }
    Ok(())
}

// Runs test defined by `suite_url`, and writes logs to writer.
// Returns (Result_, Names of tests executed, Names of tests passed, Test exit cleanly).
async fn run_test<W: std::marker::Unpin + AsyncWrite>(
    suite_url: String,
    writer: &mut W,
) -> Result<(Result_, Vec<String>, Vec<String>, bool), Error> {
    let realm = client::connect_to_service::<fsys::RealmMarker>()
        .context("could not connect to Realm service")?;

    let name = format!("test-{}", Uuid::new_v4().to_string());
    let mut collection_ref = fsys::CollectionRef { name: "tests".to_string() };
    let child_decl = fsys::ChildDecl {
        name: Some(name.clone()),
        url: Some(suite_url.clone()),
        startup: Some(fsys::StartupMode::Lazy),
        environment: None,
    };
    realm
        .create_child(&mut collection_ref, child_decl)
        .await
        .context(format!("create_child fidl call failed {}", suite_url))?
        .map_err(|e| format_err!("failed to create test: {:?} for {}", e, suite_url))?;

    let mut child_ref =
        fsys::ChildRef { name: name.clone(), collection: Some("tests".to_string()) };

    let (dir, server_end) = endpoints::create_proxy::<DirectoryMarker>().unwrap();
    realm
        .bind_child(&mut child_ref, server_end)
        .await
        .context(format!("bind_child fidl call failed for {}", name))?
        .map_err(|e| format_err!("cannot bind to test: {:?} for {}", e, name))?;

    let suite = client::connect_to_protocol_at_dir::<ftest::SuiteMarker>(&dir)
        .context("failed to open test suite service")?;

    let (sender, mut recv) = mpsc::channel(1);

    let (remote, test_fut) =
        test_executor::run_and_collect_results(suite, sender, suite_url.clone()).remote_handle();

    fasync::spawn(remote);

    let mut test_result = Result_::Passed;

    let mut test_cases_in_progress = HashSet::new();
    let mut test_cases_executed = HashSet::new();
    let mut test_cases_passed = HashSet::new();

    let mut successful_completion = false;

    while let Some(test_event) = recv.next().await {
        match test_event {
            TestEvent::TestCaseStarted { test_case_name } => {
                if test_cases_executed.contains(&test_case_name) {
                    return Err(format_err!("test case: '{}' started twice", test_case_name));
                }
                writer
                    .write(format!("[RUNNING]\t{}\n", test_case_name).as_bytes())
                    .await
                    .context("Can't write output")?;
                test_cases_in_progress.insert(test_case_name.clone());
                test_cases_executed.insert(test_case_name);
            }
            TestEvent::TestCaseFinished { test_case_name, result } => {
                if !test_cases_in_progress.contains(&test_case_name) {
                    return Err(format_err!(
                        "test case: '{}' was never started, still got a finish event",
                        test_case_name
                    ));
                }
                test_cases_in_progress.remove(&test_case_name);
                let result_str = match result {
                    test_executor::TestResult::Passed => {
                        test_cases_passed.insert(test_case_name.clone());
                        "PASSED".to_string()
                    }
                    test_executor::TestResult::Failed => {
                        if test_result == Result_::Passed {
                            test_result = Result_::Failed;
                        }
                        "FAILED".to_string()
                    }
                    test_executor::TestResult::Skipped => "SKIPPED".to_string(),
                    test_executor::TestResult::Error => {
                        test_result = Result_::Error;
                        "ERROR".to_string()
                    }
                };
                writer
                    .write(format!("[{}]\t{}\n", result_str, test_case_name).as_bytes())
                    .await
                    .context("Can't write output")?;
            }
            TestEvent::LogMessage { test_case_name, msg } => {
                if !test_cases_executed.contains(&test_case_name) {
                    return Err(format_err!(
                        "test case: '{}' was never started, still got a log",
                        test_case_name
                    ));
                }
                let msgs = msg.trim().split("\n");
                for msg in msgs {
                    writer
                        .write(format!("[{}]\t{}\n", test_case_name, msg).as_bytes())
                        .await
                        .context("Can't write output")?;
                }
            }
            TestEvent::Finish => {
                successful_completion = true;
                break;
            }
        }
    }

    test_fut.await.map_err(|e| format_err!("Error running test: {}", e))?;

    let mut test_cases_in_progress: Vec<String> = test_cases_in_progress.into_iter().collect();
    test_cases_in_progress.sort();

    if test_cases_in_progress.len() != 0 {
        match test_result {
            Result_::Passed | Result_::Failed => {
                test_result = Result_::Inconclusive;
            }
            _ => {}
        }
        writer
            .write(format!("\nThe following test(s) never completed:\n").as_bytes())
            .await
            .context("Can't write output")?;
        for t in test_cases_in_progress {
            writer.write(format!("{}\n", t).as_bytes()).await.context("Can't write output")?;
        }
    }

    let mut test_cases_executed: Vec<String> = test_cases_executed.into_iter().collect();
    let mut test_cases_passed: Vec<String> = test_cases_passed.into_iter().collect();

    test_cases_executed.sort();
    test_cases_passed.sort();

    Ok((test_result, test_cases_executed, test_cases_passed, successful_completion))
}
