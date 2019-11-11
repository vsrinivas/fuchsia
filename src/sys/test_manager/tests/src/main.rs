// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This unit tests a library which uses v2 framework APIs, so it needs to be launched as a
//! v2 component.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_test as ftest, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
    std::str::from_utf8,
};

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("error creating executor")?;
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn_local(async move {
            run_test_suite(stream).await.expect("failed to run test suite service")
        });
    });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}

async fn run_test(
    logger: fuchsia_zircon::Socket,
    outcome: &mut ftest::Outcome,
) -> Result<(), Error> {
    let test_url = "fuchsia-pkg://fuchsia.com/example-tests#meta/echo_test_realm.cm";

    let mut output: Vec<u8> = vec![];
    let (result, executed, passed) = test_manager_lib::run_test(test_url.to_string(), &mut output)
        .await
        .expect("error trying to run echo tests");

    let expected_output = "[RUNNING]	EchoTest
[PASSED]	EchoTest
";

    if from_utf8(&output) != Ok(expected_output) {
        logger.write(
            format!("expected output: {}, got: {:?}", expected_output, from_utf8(&output))
                .as_bytes(),
        )?;
        outcome.status = Some(ftest::Status::Failed);
    }

    if result != test_manager_lib::TestOutcome::Passed {
        logger.write(format!("echo test failed with status {:?}", result).as_bytes())?;
        outcome.status = Some(ftest::Status::Failed);
    }

    if executed != vec!["EchoTest"] {
        logger.write(
            format!("expected executed: {:?}, got: {:?}", vec!["EchoTest"], executed).as_bytes(),
        )?;
        outcome.status = Some(ftest::Status::Failed);
    }

    if passed != vec!["EchoTest"] {
        logger.write(
            format!("expected passed: {:?}, got: {:?}", vec!["EchoTest"], passed).as_bytes(),
        )?;
        outcome.status = Some(ftest::Status::Failed);
    }

    Ok(())
}

// This implementation should eventually merge with rust test framework and we should be able to
// run this  test as a normal rust test.
async fn run_test_suite(mut stream: ftest::SuiteRequestStream) -> Result<(), Error> {
    let test_name = "TestManager.CanRunTest";
    while let Some(event) = stream.try_next().await? {
        match event {
            ftest::SuiteRequest::GetTests { responder } => {
                responder
                    .send(&mut vec![ftest::Case { name: Some(test_name.to_string()) }].into_iter())
                    .expect("Should not error out");
            }
            ftest::SuiteRequest::Run { tests, options: _, run_listener, .. } => {
                assert_eq!(tests.len(), 1);
                assert_eq!(tests[0].case, Some(ftest::Case { name: Some(test_name.to_string()) }));

                let proxy =
                    run_listener.into_proxy().expect("Can't convert listener channel to proxy");
                let (log_end, logger) =
                    fuchsia_zircon::Socket::create(fuchsia_zircon::SocketOpts::empty())
                        .expect("cannot create socket.");
                let mut outcome = ftest::Outcome { status: Some(ftest::Status::Passed) };

                proxy
                    .on_test_case_started(test_name, log_end)
                    .expect("on_test_case_started failed");
                run_test(logger, &mut outcome).await?;
                proxy
                    .on_test_case_finished(test_name, outcome)
                    .expect("on_test_case_finished failed");
            }
        }
    }
    Ok(())
}
