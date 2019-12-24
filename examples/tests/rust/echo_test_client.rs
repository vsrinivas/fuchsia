// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fidl_examples_routing_echo as fecho, fidl_fuchsia_test as ftest, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
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

async fn run_echo(echo_str: &str, outcome: &mut ftest::Outcome) -> Result<(), Error> {
    let echo = connect_to_service::<fecho::EchoMarker>().context("error connecting to echo")?;
    match echo.echo_string(Some(echo_str)).await {
        Ok(result) => {
            if result != Some(echo_str.to_string()) {
                outcome.status = Some(ftest::Status::Failed);
                println!("Echo failed, expected: {}, got: {:?}", echo_str, result);
            }
        }
        Err(e) => {
            outcome.status = Some(ftest::Status::Failed);
            println!("Echo failed: {}", e);
        }
    }
    Ok(())
}

// This implementation should eventually merge with rust test framework and we should be able to
// run this  test as a normal rust test.
async fn run_test_suite(mut stream: ftest::SuiteRequestStream) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            ftest::SuiteRequest::GetTests { responder } => {
                responder
                    .send(&mut vec![ftest::Case { name: Some("EchoTest".to_string()) }].into_iter())
                    .expect("Should not error out");
            }
            ftest::SuiteRequest::Run { tests, options: _, run_listener, .. } => {
                assert_eq!(tests.len(), 1);
                assert_eq!(tests[0].case, Some(ftest::Case { name: Some("EchoTest".to_string()) }));

                let proxy =
                    run_listener.into_proxy().expect("Can't convert listener channel to proxy");
                let (log_end, _logger) =
                    fuchsia_zircon::Socket::create(fuchsia_zircon::SocketOpts::empty())
                        .expect("cannot create socket.");
                let mut outcome = ftest::Outcome { status: Some(ftest::Status::Passed) };

                proxy
                    .on_test_case_started("EchoTest", log_end)
                    .expect("on_test_case_started failed");
                run_echo("test_string1", &mut outcome).await?;
                run_echo("test_string2", &mut outcome).await?;
                proxy
                    .on_test_case_finished("EchoTest", outcome)
                    .expect("on_test_case_finished failed");
            }
        }
    }
    Ok(())
}
