// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error, ResultExt},
    fidl_fidl_test_compatibility::{
        EchoEchoArraysWithErrorResult, EchoEchoStructWithErrorResult, EchoEchoTableWithErrorResult,
        EchoEchoVectorsWithErrorResult, EchoEchoXunionsWithErrorResult, EchoEvent, EchoMarker,
        EchoProxy, EchoRequest, EchoRequestStream, RespondWith,
    },
    fidl_fuchsia_sys::LauncherProxy,
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{launch, launcher, App},
        server::ServiceFs,
    },
    futures::{StreamExt, TryStreamExt},
    std::thread,
};

fn launch_and_connect_to_echo(
    launcher: &LauncherProxy,
    url: String,
) -> Result<(EchoProxy, App), Error> {
    let app = launch(&launcher, url, None)?;
    let echo = app.connect_to_service::<EchoMarker>()?;
    Ok((echo, app))
}

async fn echo_server(stream: EchoRequestStream, launcher: &LauncherProxy) -> Result<(), Error> {
    let handler = move |request| {
        Box::pin(async move {
            match request {
                EchoRequest::EchoStruct { mut value, forward_to_server, responder } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        value = echo
                            .echo_struct(&mut value, "")
                            .await
                            .context("Error calling echo_struct on proxy")?;
                        drop(app);
                    }
                    responder.send(&mut value).context("Error responding")?;
                }
                EchoRequest::EchoStructWithError {
                    mut value,
                    result_err,
                    forward_to_server,
                    result_variant,
                    responder,
                } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        let mut result = echo
                            .echo_struct_with_error(&mut value, result_err, "", result_variant)
                            .await
                            .context("Error calling echo_struct_with_error on proxy")?;
                        drop(app);
                        responder.send(&mut result).context("Error responding")?;
                    } else {
                        let mut result = if let RespondWith::Err = result_variant {
                            EchoEchoStructWithErrorResult::Err(result_err)
                        } else {
                            EchoEchoStructWithErrorResult::Ok(value)
                        };
                        responder.send(&mut result).context("Error responding")?;
                    }
                }
                EchoRequest::EchoStructNoRetVal {
                    mut value,
                    forward_to_server,
                    control_handle,
                } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        echo.echo_struct_no_ret_val(&mut value, "")
                            .context("Error sending echo_struct_no_ret_val to proxy")?;
                        let mut event_stream = echo.take_event_stream();
                        let EchoEvent::EchoEvent { value: response_val } = event_stream
                            .try_next()
                            .await
                            .context("Error getting event response from proxy")?
                            .ok_or_else(|| format_err!("Proxy sent no events"))?;
                        value = response_val;
                        drop(app);
                    }
                    control_handle
                        .send_echo_event(&mut value)
                        .context("Error responding with event")?;
                }
                EchoRequest::EchoArrays { mut value, forward_to_server, responder } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        value = echo
                            .echo_arrays(&mut value, "")
                            .await
                            .context("Error calling echo_arrays on proxy")?;
                        drop(app);
                    }
                    responder.send(&mut value).context("Error responding")?;
                }
                EchoRequest::EchoArraysWithError {
                    mut value,
                    result_err,
                    forward_to_server,
                    result_variant,
                    responder,
                } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        let mut result = echo
                            .echo_arrays_with_error(&mut value, result_err, "", result_variant)
                            .await
                            .context("Error calling echo_struct_with_error on proxy")?;
                        drop(app);
                        responder.send(&mut result).context("Error responding")?;
                    } else {
                        let mut result = if let RespondWith::Err = result_variant {
                            EchoEchoArraysWithErrorResult::Err(result_err)
                        } else {
                            EchoEchoArraysWithErrorResult::Ok(value)
                        };
                        responder.send(&mut result).context("Error responding")?;
                    }
                }
                EchoRequest::EchoVectors { mut value, forward_to_server, responder } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        value = echo
                            .echo_vectors(&mut value, "")
                            .await
                            .context("Error calling echo_vectors on proxy")?;
                        drop(app);
                    }
                    responder.send(&mut value).context("Error responding")?;
                }
                EchoRequest::EchoVectorsWithError {
                    mut value,
                    result_err,
                    forward_to_server,
                    result_variant,
                    responder,
                } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        let mut result = echo
                            .echo_vectors_with_error(&mut value, result_err, "", result_variant)
                            .await
                            .context("Error calling echo_struct_with_error on proxy")?;
                        drop(app);
                        responder.send(&mut result).context("Error responding")?;
                    } else {
                        let mut result = if let RespondWith::Err = result_variant {
                            EchoEchoVectorsWithErrorResult::Err(result_err)
                        } else {
                            EchoEchoVectorsWithErrorResult::Ok(value)
                        };
                        responder.send(&mut result).context("Error responding")?;
                    }
                }
                EchoRequest::EchoTable { mut value, forward_to_server, responder } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        value = echo
                            .echo_table(value, "")
                            .await
                            .context("Error calling echo_table on proxy")?;
                        drop(app);
                    }
                    responder.send(value).context("Error responding")?;
                }
                EchoRequest::EchoTableWithError {
                    value,
                    result_err,
                    forward_to_server,
                    result_variant,
                    responder,
                } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        let mut result = echo
                            .echo_table_with_error(value, result_err, "", result_variant)
                            .await
                            .context("Error calling echo_struct_with_error on proxy")?;
                        drop(app);
                        responder.send(&mut result).context("Error responding")?;
                    } else {
                        let mut result = if let RespondWith::Err = result_variant {
                            EchoEchoTableWithErrorResult::Err(result_err)
                        } else {
                            EchoEchoTableWithErrorResult::Ok(value)
                        };
                        responder.send(&mut result).context("Error responding")?;
                    }
                }
                EchoRequest::EchoXunions { mut value, forward_to_server, responder } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        value = echo
                            .echo_xunions(&mut value.iter_mut(), "")
                            .await
                            .context("Error calling echo_xunions on proxy")?;
                        drop(app);
                    }
                    responder.send(&mut value.iter_mut()).context("Error responding")?;
                }
                EchoRequest::EchoXunionsWithError {
                    mut value,
                    result_err,
                    forward_to_server,
                    result_variant,
                    responder,
                } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        let mut result = echo
                            .echo_xunions_with_error(
                                &mut value.iter_mut(),
                                result_err,
                                "",
                                result_variant,
                            )
                            .await
                            .context("Error calling echo_struct_with_error on proxy")?;
                        drop(app);
                        responder.send(&mut result).context("Error responding")?;
                    } else {
                        let mut result = if let RespondWith::Err = result_variant {
                            EchoEchoXunionsWithErrorResult::Err(result_err)
                        } else {
                            EchoEchoXunionsWithErrorResult::Ok(value)
                        };
                        responder.send(&mut result).context("Error responding")?;
                    }
                }
            }
            Ok(())
        })
    };

    let handle_requests_fut = stream
        .err_into() // change error type from fidl::Error to failure::Error
        .try_for_each_concurrent(None /* max concurrent requests per connection */, handler);

    handle_requests_fut.await
}

fn main() -> Result<(), Error> {
    let argv: Vec<String> = std::env::args().collect();
    println!("argv={:?}", argv);

    const STACK_SIZE: usize = 512 * 1024;

    // Create a child thread with a larger stack size to accomodate large structures being built.
    let thread_handle = thread::Builder::new().stack_size(STACK_SIZE).spawn(run_test)?;

    thread_handle.join().expect("Failed to join test thread")
}

fn run_test() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let launcher = launcher().context("Error connecting to application launcher")?;

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(|stream| stream);
    fs.take_and_serve_directory_handle().context("Error serving directory handle")?;

    let serve_fut =
        fs.for_each_concurrent(None /* max concurrent connections */, |stream| async {
            if let Err(e) = echo_server(stream, &launcher).await {
                eprintln!("Closing echo server {:?}", e);
            }
        });

    executor.run_singlethreaded(serve_fut);
    Ok(())
}
