// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error, ResultExt},
    fidl::endpoints,
    fidl_fuchsia_io::{DirectoryMarker, MODE_TYPE_SERVICE},
    fidl_fuchsia_sys2 as fsys, fidl_fuchsia_test_manager as ftest_manager,
    ftest_manager::Outcome,
    fuchsia_async as fasync,
    fuchsia_component::client,
    futures::io::AsyncWrite,
    futures::{channel::mpsc, prelude::*},
    io_util::{self, OPEN_RIGHT_READABLE},
    std::collections::HashSet,
    std::path::PathBuf,
    test_executor::TestEvent,
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
                    responder.send(Outcome::Error).context("Can't send result back")?;
                    continue;
                }

                let (outcome, executed, passed) = result.unwrap();

                logger
                    .write(
                        format!("\n{} out of {} tests passed...\n", passed.len(), executed.len())
                            .as_bytes(),
                    )
                    .await
                    .context("Can't write output")?;
                logger
                    .write(
                        format!("{} completed with outcome: {:?}\n", suite_url, outcome).as_bytes(),
                    )
                    .await
                    .context("Can't write output")?;

                responder.send(outcome).context("Can't send result back")?;
            }
        }
    }
    Ok(())
}

// Runs test defined by `suite_url`, and writes logs to writer.
// Returns (Outcome, Names of tests executed, Names of tests passed).
async fn run_test<W: std::marker::Unpin + AsyncWrite>(
    suite_url: String,
    writer: &mut W,
) -> Result<(Outcome, Vec<String>, Vec<String>), Error> {
    let realm = client::connect_to_service::<fsys::RealmMarker>()
        .context("could not connect to Realm service")?;

    let name = "name".to_owned();
    let mut collection_ref = fsys::CollectionRef { name: "tests".to_string() };
    let child_decl = fsys::ChildDecl {
        name: Some(name.clone()),
        url: Some(suite_url.clone()),
        startup: Some(fsys::StartupMode::Lazy),
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

    let node_proxy = io_util::open_node(
        &dir,
        &PathBuf::from("svc/fuchsia.test.Suite"),
        OPEN_RIGHT_READABLE,
        MODE_TYPE_SERVICE,
    )
    .context("failed to open test suite service")?;
    let suite = fidl_fuchsia_test::SuiteProxy::new(node_proxy.into_channel().unwrap());

    let (sender, mut recv) = mpsc::channel(1);

    let (remote, test_fut) =
        test_executor::run_and_collect_results(suite, sender, suite_url.clone()).remote_handle();

    fasync::spawn(remote);

    let mut test_outcome = Outcome::Passed;

    let mut test_cases_in_progress = HashSet::new();
    let mut test_cases_executed = HashSet::new();
    let mut test_cases_passed = HashSet::new();

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
            TestEvent::TestCaseFinished { test_case_name, outcome } => {
                if !test_cases_in_progress.contains(&test_case_name) {
                    return Err(format_err!(
                        "test case: '{}' was never started, still got a finish event",
                        test_case_name
                    ));
                }
                test_cases_in_progress.remove(&test_case_name);
                let outcome_str = match outcome {
                    test_executor::Outcome::Passed => {
                        test_cases_passed.insert(test_case_name.clone());
                        "PASSED".to_string()
                    }
                    test_executor::Outcome::Failed => {
                        if test_outcome == Outcome::Passed {
                            test_outcome = Outcome::Failed;
                        }
                        "FAILED".to_string()
                    }
                    test_executor::Outcome::Error => {
                        test_outcome = Outcome::Error;
                        "ERROR".to_string()
                    }
                };
                writer
                    .write(format!("[{}]\t{}\n", outcome_str, test_case_name).as_bytes())
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
        }
    }

    test_fut.await.map_err(|e| format_err!("Error running test: {}", e))?;

    let mut test_cases_in_progress: Vec<String> = test_cases_in_progress.into_iter().collect();
    test_cases_in_progress.sort();

    if test_cases_in_progress.len() != 0 {
        match test_outcome {
            Outcome::Passed | Outcome::Failed => {
                test_outcome = Outcome::Inconclusive;
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

    Ok((test_outcome, test_cases_executed, test_cases_passed))
}
