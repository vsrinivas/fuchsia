// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl_fidl_examples_routing_echo as fecho, fidl_fuchsia_test as ftest, fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
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

async fn run_echo(echo_str: &str, result: &mut ftest::Result_) -> Result<(), Error> {
    let echo = connect_to_service::<fecho::EchoMarker>().context("error connecting to echo")?;
    match echo.echo_string(Some(echo_str)).await {
        Ok(reply) => {
            if reply != Some(echo_str.to_string()) {
                result.status = Some(ftest::Status::Failed);
                println!("Echo failed, expected: {}, got: {:?}", echo_str, reply);
            }
        }
        Err(e) => {
            result.status = Some(ftest::Status::Failed);
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
            ftest::SuiteRequest::GetTests { iterator, control_handle: _ } => {
                let mut stream = iterator.into_stream()?;
                fasync::spawn(
                    async move {
                        let mut cases_iter =
                            vec![ftest::Case { name: Some("EchoTest".to_string()) }].into_iter();
                        while let Some(ftest::CaseIteratorRequest::GetNext { responder }) =
                            stream.try_next().await?
                        {
                            const MAX_CASES_PER_PAGE: usize = 50;
                            responder.send(&mut cases_iter.by_ref().take(MAX_CASES_PER_PAGE))?;
                        }
                        Ok(())
                    }
                    .unwrap_or_else(|e: anyhow::Error| println!("error serving tests: {:?}", e)),
                );
            }
            ftest::SuiteRequest::Run { mut tests, options: _, listener, .. } => {
                assert_eq!(tests.len(), 1);
                assert_eq!(tests[0].name, Some("EchoTest".to_string()));

                let proxy = listener.into_proxy().expect("Can't convert listener channel to proxy");
                let (log_end, _logger) =
                    fuchsia_zircon::Socket::create(fuchsia_zircon::SocketOpts::empty())
                        .expect("cannot create socket.");
                let mut result = ftest::Result_ { status: Some(ftest::Status::Passed) };

                let (case_listener_proxy, case_listener) =
                    fidl::endpoints::create_proxy::<fidl_fuchsia_test::CaseListenerMarker>()
                        .expect("cannot create proxy");
                proxy
                    .on_test_case_started(tests.pop().unwrap(), log_end, case_listener)
                    .expect("on_test_case_started failed");
                run_echo("test_string1", &mut result).await?;
                run_echo("test_string2", &mut result).await?;

                case_listener_proxy.finished(result).expect("on_test_case_finished failed");

                proxy.on_finished().expect("on_finished failed");
            }
        }
    }
    Ok(())
}
