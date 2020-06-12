// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, format_err, Context, Error},
    ffx_core::ffx_plugin,
    ffx_test_args::TestCommand,
    fidl::endpoints::create_proxy,
    fidl_fuchsia_test::{CaseIteratorMarker, Invocation, SuiteProxy},
    fidl_fuchsia_test_manager as ftest_manager,
    futures::{channel::mpsc, FutureExt, StreamExt},
    regex::Regex,
    std::io::{stdout, Write},
    test_executor::{
        run_and_collect_results_for_invocations as run_tests_and_collect, TestEvent, TestResult,
    },
};

#[ffx_plugin(ftest_manager::HarnessProxy = "core/appmgr:out:fuchsia.test.manager.Harness")]
pub async fn test(
    harness_proxy: ftest_manager::HarnessProxy,
    cmd: TestCommand,
) -> Result<(), Error> {
    let writer = Box::new(stdout());
    if cmd.list {
        get_tests(harness_proxy, writer, &cmd.url).await
    } else {
        run_tests(harness_proxy, writer, &cmd.url, &cmd.tests).await
    }
}

async fn get_tests<W: Write>(
    harness_proxy: ftest_manager::HarnessProxy,
    mut write: W,
    suite_url: &String,
) -> Result<(), Error> {
    let writer = &mut write;
    let (suite_proxy, suite_server_end) = create_proxy().unwrap();
    let (_controller_proxy, controller_server_end) = create_proxy().unwrap();

    log::info!("launching test suite {}", suite_url);

    let _result = harness_proxy
        .launch_suite(
            &suite_url,
            ftest_manager::LaunchOptions {},
            suite_server_end,
            controller_server_end,
        )
        .await
        .context("launch_suite call failed")?
        .map_err(|e| format_err!("error launching test: {:?}", e))?;

    let (case_iterator, test_server_end) = create_proxy::<CaseIteratorMarker>()?;
    suite_proxy
        .get_tests(test_server_end)
        .map_err(|e| format_err!("Error getting test steps: {}", e))?;

    loop {
        let cases = case_iterator.get_next().await?;
        if cases.is_empty() {
            return Ok(());
        }
        writeln!(writer, "Tests in suite {}:\n", suite_url)?;
        for case in cases {
            match case.name {
                Some(n) => writeln!(writer, "{}", n)?,
                None => writeln!(writer, "<No name>")?,
            };
        }
    }
}

async fn get_invocations(
    suite: &SuiteProxy,
    test_selector: &Option<Regex>,
) -> Result<Vec<Invocation>, Error> {
    let (case_iterator, server_end) = create_proxy()?;
    suite.get_tests(server_end).map_err(|e| format_err!("Error getting test steps: {}", e))?;

    let mut invocations = Vec::<Invocation>::new();
    loop {
        let cases = case_iterator.get_next().await?;
        if cases.is_empty() {
            break;
        }
        for case in cases {
            // TODO: glob type pattern matching would probably be better than regex - maybe
            // both? Will update after meeting with UX.
            let test_case_name = case.name.unwrap();
            match &test_selector {
                Some(s) => {
                    if s.is_match(&test_case_name) {
                        invocations.push(Invocation { name: Some(test_case_name), tag: None });
                    }
                }
                None => invocations.push(Invocation { name: Some(test_case_name), tag: None }),
            }
        }
    }
    Ok(invocations)
}

async fn run_tests<W: Write>(
    harness_proxy: ftest_manager::HarnessProxy,
    mut write: W,
    suite_url: &String,
    tests: &Option<String>,
) -> Result<(), Error> {
    let writer = &mut write;
    let (suite_proxy, suite_server_end) = create_proxy().expect("creating suite proxy");
    let (_controller_proxy, controller_server_end) =
        create_proxy().expect("creating controller proxy");

    let test_selector = match tests {
        Some(s) => match Regex::new(s) {
            Ok(r) => Some(r),
            Err(e) => {
                return Err(anyhow!("invalid regex for tests: \"{}\"\n{}", s, e));
            }
        },
        None => None,
    };

    log::info!("launching test suite {}", suite_url);
    writeln!(writer, "*** Launching {} ***", suite_url)?;

    let _result = harness_proxy
        .launch_suite(
            &suite_url,
            ftest_manager::LaunchOptions {},
            suite_server_end,
            controller_server_end,
        )
        .await
        .context("launch_test call failed")?
        .map_err(|e| format_err!("error launching test: {:?}", e))?;

    log::info!("launched suite, getting tests");
    let (sender, recv) = mpsc::channel(1);

    writeln!(writer, "Getting tests...")?;
    let invocations = get_invocations(&suite_proxy, &test_selector).await?;
    if invocations.is_empty() {
        match tests {
            Some(test_selector) => writeln!(writer, "No test cases match {}", test_selector)?,
            None => writeln!(writer, "No tests cases found in suite {}", suite_url)?,
        };
        return Ok(());
    }
    writeln!(writer, "Running tests...")?;
    let (successful_completion, ()) = futures::future::try_join(
        collect_events(writer, recv).map(Ok),
        run_tests_and_collect(suite_proxy, sender, invocations),
    )
    .await
    .context("running test")?;

    if !successful_completion {
        return Err(anyhow!("Test run finished prematurely. Something went wrong."));
    }
    writeln!(writer, "*** Finished {} ***", suite_url)?;
    Ok(())
}

async fn collect_events<W: Write>(writer: &mut W, mut recv: mpsc::Receiver<TestEvent>) -> bool {
    let mut successful_completion = false;
    while let Some(event) = recv.next().await {
        match event {
            TestEvent::LogMessage { test_case_name, msg } => {
                let logs = msg.split("\n");
                for log in logs {
                    if log.len() > 0 {
                        writeln!(writer, "{}: {}", test_case_name, log.to_string())
                            .expect("writing to output")
                    }
                }
            }
            TestEvent::TestCaseStarted { test_case_name } => {
                writeln!(writer, "[RUNNING]\t{}", test_case_name).expect("writing to output");
            }
            TestEvent::TestCaseFinished { test_case_name, result } => {
                match result {
                    TestResult::Passed => {
                        writeln!(writer, "[PASSED]\t{}", test_case_name).expect("writing to output")
                    }
                    TestResult::Failed => {
                        writeln!(writer, "[FAILED]\t{}", test_case_name).expect("writing to output")
                    }
                    TestResult::Skipped => writeln!(writer, "[SKIPPED]\t{}", test_case_name)
                        .expect("writing to output"),
                    TestResult::Error => {
                        writeln!(writer, "[ERROR]\t{}", test_case_name).expect("writing to output")
                    }
                };
            }
            TestEvent::Finish => {
                successful_completion = true;
            }
        };
    }
    successful_completion
}

////////////////////////////////////////////////////////////////////////////////
// tests
#[cfg(test)]
mod test {
    use {
        super::*,
        fidl_fuchsia_test::{
            Case, CaseIteratorRequest, CaseIteratorRequestStream, CaseListenerMarker, Result_,
            Status, SuiteRequest, SuiteRequestStream,
        },
        fidl_fuchsia_test_manager::{HarnessMarker, HarnessProxy, HarnessRequest},
        futures::TryStreamExt,
        std::io::BufWriter,
    };

    fn setup_fake_harness_service_with_tests(num_tests: usize) -> HarnessProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<HarnessMarker>().unwrap();

        hoist::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    HarnessRequest::LaunchSuite {
                        test_url: _,
                        suite,
                        options: _,
                        controller: _,
                        responder,
                    } => {
                        let suite_request_stream = suite.into_stream().unwrap();
                        spawn_fake_suite_server(suite_request_stream, num_tests);
                        let _ = responder.send(&mut Ok(()));
                    }
                }
            }
        });

        proxy
    }

    fn spawn_fake_suite_server(mut stream: SuiteRequestStream, num_tests: usize) {
        hoist::spawn(async move {
            while let Ok(Some(req)) = stream.try_next().await {
                match req {
                    SuiteRequest::GetTests { iterator, control_handle: _ } => {
                        let values: Vec<String> =
                            (0..num_tests).map(|i| format!("Test {}", i)).collect();
                        let iterator_request_stream = iterator.into_stream().unwrap();
                        spawn_fake_iterator_server(values, iterator_request_stream);
                    }
                    SuiteRequest::Run { mut tests, options: _, listener, .. } => {
                        let listener = listener
                            .into_proxy()
                            .context("Can't convert listener into proxy")
                            .unwrap();
                        tests.iter_mut().for_each(|t| {
                            let (log, client_log) =
                                fidl::Socket::create(fidl::SocketOpts::DATAGRAM)
                                    .context("failed to create socket")
                                    .unwrap();
                            let (case_listener, client_end) =
                                create_proxy::<CaseListenerMarker>().unwrap();
                            listener
                                .on_test_case_started(
                                    Invocation { name: t.name.take(), tag: None },
                                    client_log,
                                    client_end,
                                )
                                .context("Cannot send on_test_case_started")
                                .unwrap();
                            log.write(b"Test log message\n").unwrap();
                            case_listener
                                .finished(Result_ { status: Some(Status::Passed) })
                                .context("Cannot send finished")
                                .unwrap();
                        });
                        listener.on_finished().context("Cannot send on_finished event").unwrap();
                    }
                }
            }
        });
    }

    fn spawn_fake_iterator_server(values: Vec<String>, mut stream: CaseIteratorRequestStream) {
        let mut iter = values.into_iter().map(|name| Case { name: Some(name) });
        hoist::spawn(async move {
            while let Ok(Some(CaseIteratorRequest::GetNext { responder })) = stream.try_next().await
            {
                responder.send(&mut iter.by_ref().take(50)).unwrap();
            }
        });
    }

    #[test]
    fn test_list_tests() {
        let mut output = String::new();
        let url = "fuchsia-pkg://fuchsia.com/dummy-package#meta/echo_test_realm.cm".to_string();
        let num_tests = 50;
        let test = Regex::new(r"Test [0-9+]").expect("test regex");
        hoist::run(async move {
            let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            let harness_proxy = setup_fake_harness_service_with_tests(num_tests);
            let _response = get_tests(harness_proxy, writer, &url)
                .await
                .expect("getting tests should not fail");
            assert_eq!(num_tests, test.find_iter(&output).count());
        });
    }

    fn test_run(
        num_tests: usize,
        expected_run: usize,
        selector: Option<String>,
    ) -> Result<(), Error> {
        let mut output = String::new();
        let url = "fuchsia-pkg://fuchsia.com/dummy-package#meta/echo_test_realm.cm".to_string();
        hoist::run(async move {
            let writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            let harness_proxy = setup_fake_harness_service_with_tests(num_tests);
            let _response = run_tests(harness_proxy, writer, &url, &selector)
                .await
                .expect("run tests should not fail");
            let test_running = Regex::new(r"RUNNING").expect("test regex");
            assert_eq!(expected_run, test_running.find_iter(&output).count());
            let test_passed = Regex::new(r"PASSED").expect("test regex");
            assert_eq!(expected_run, test_passed.find_iter(&output).count());
        });
        Ok(())
    }

    #[test]
    fn test_run_tests() -> Result<(), Error> {
        test_run(100, 100, None)
    }

    #[test]
    fn test_run_tests_with_selector() -> Result<(), Error> {
        test_run(100, 19, Some("6".to_string()))
    }

    #[test]
    fn test_run_tests_with_unmatched_selector() -> Result<(), Error> {
        test_run(100, 0, Some("Echo".to_string()))
    }

    #[test]
    fn test_run_tests_with_invalid_selector() -> Result<(), Error> {
        let mut output = String::new();
        let url = "fuchsia-pkg://fuchsia.com/dummy-package#meta/echo_test_realm.cm".to_string();
        let selector = Some("[".to_string());
        let num_tests = 1;
        hoist::run(async move {
            let mut writer = unsafe { BufWriter::new(output.as_mut_vec()) };
            let harness_proxy = setup_fake_harness_service_with_tests(num_tests);
            let response = run_tests(harness_proxy, &mut writer, &url, &selector).await;
            assert!(response.is_err());
        });
        Ok(())
    }
}
