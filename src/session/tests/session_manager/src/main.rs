// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_test as ftest, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
    session_manager_lib,
};

/// The name of this test.
const TEST_NAME: &str = "SessionManagerTest";

/// Exposes the TestSuite service for the test_executor to collect test results.
/// Passes if the root session launches successfully. This tells us:
///     - argument parsing succeeded and the root session url was extracted from it.
///     - session_manager is able to use the Realm service to launch a component.
///     - the root session was started in the "session" collection.
///     - capability routing of the Scenic service to the session collection was successful.
fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor.")?;
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream: ftest::SuiteRequestStream| {
        fasync::spawn_local(async move {
            run_test_suite(stream).await.expect("Failed to run test suite service.")
        });
    });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());

    Ok(())
}

/// Calls session_manager library to run a session.
///
/// `outcome`: The outcome of running a session, either Ok(()) or an Err()
async fn run_session(outcome: &mut ftest::Outcome) -> Result<(), Error> {
    if let Some(session_url) = session_manager_lib::startup::get_session_url() {
        match session_manager_lib::startup::launch_session(&session_url).await {
            Ok(_) => {}
            Err(e) => {
                outcome.status = Some(ftest::Status::Failed);
                println!("Run session failed: {}", e);
            }
        }
    }
    Ok(())
}

/// This runs the TestSuite service and the SessionManagerTest.
///
/// This implementation should eventually merge with rust test framework and we should be able to
/// run this  test as a normal rust test.
async fn run_test_suite(mut stream: ftest::SuiteRequestStream) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            ftest::SuiteRequest::GetTests { responder } => {
                responder
                    .send(&mut vec![ftest::Case { name: Some(TEST_NAME.to_string()) }].into_iter())
                    .expect("Should not error out");
            }
            ftest::SuiteRequest::Run { tests: _, options: _, run_listener, .. } => {
                let proxy =
                    run_listener.into_proxy().expect("Can't convert listener channel to proxy");
                let (log_end, _logger) =
                    fuchsia_zircon::Socket::create(fuchsia_zircon::SocketOpts::empty())
                        .expect("cannot create socket.");
                let mut outcome = ftest::Outcome { status: Some(ftest::Status::Passed) };

                proxy
                    .on_test_case_started(TEST_NAME, log_end)
                    .expect("on_test_case_started failed");
                run_session(&mut outcome).await?;
                proxy
                    .on_test_case_finished(TEST_NAME, outcome)
                    .expect("on_test_case_finished failed");
            }
        }
    }
    Ok(())
}
