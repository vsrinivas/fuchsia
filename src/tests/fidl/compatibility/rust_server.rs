// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fidl_test_compatibility::{
        EchoEchoArraysWithErrorResult, EchoEchoMinimalWithErrorResult,
        EchoEchoNamedStructWithErrorResult, EchoEchoStructWithErrorResult,
        EchoEchoTablePayloadWithErrorResult, EchoEchoTableWithErrorResult,
        EchoEchoUnionPayloadWithErrorRequest, EchoEchoUnionPayloadWithErrorRequestUnknown,
        EchoEchoUnionPayloadWithErrorResult, EchoEchoVectorsWithErrorResult,
        EchoEchoXunionsWithErrorResult, EchoEvent, EchoMarker, EchoProxy, EchoRequest,
        EchoRequestStream, RequestUnion, RequestUnionUnknown, RespondWith, ResponseTable,
        ResponseUnion,
    },
    fidl_fidl_test_imported::{
        ComposedEchoUnionResponseWithErrorComposedResponse,
        ComposedEchoUnionResponseWithErrorComposedResult, SimpleStruct, WantResponse,
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
    let echo = app.connect_to_protocol::<EchoMarker>()?;
    Ok((echo, app))
}

async fn echo_server(stream: EchoRequestStream, launcher: &LauncherProxy) -> Result<(), Error> {
    let handler = move |request| {
        Box::pin(async move {
            match request {
                EchoRequest::EchoMinimal { forward_to_server, responder } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        echo.echo_minimal("")
                            .await
                            .context("Error calling echo_minimal on proxy")?;
                        drop(app);
                    }
                    responder.send().context("Error responding")?;
                }
                EchoRequest::EchoMinimalWithError {
                    forward_to_server,
                    result_variant,
                    responder,
                } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        let mut result = echo
                            .echo_minimal_with_error("", result_variant)
                            .await
                            .context("Error calling echo_minimal_with_error on proxy")?;
                        drop(app);
                        responder.send(&mut result).context("Error responding")?;
                    } else {
                        let mut result = if let RespondWith::Err = result_variant {
                            EchoEchoMinimalWithErrorResult::Err(0)
                        } else {
                            EchoEchoMinimalWithErrorResult::Ok(())
                        };
                        responder.send(&mut result).context("Error responding")?;
                    }
                }
                EchoRequest::EchoMinimalNoRetVal { forward_to_server, control_handle } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        echo.echo_minimal_no_ret_val("")
                            .context("Error sending echo_minimal_no_ret_val to proxy")?;
                        let mut event_stream = echo.take_event_stream();
                        match event_stream
                            .try_next()
                            .await
                            .context("Error getting event response from proxy")?
                            .ok_or_else(|| format_err!("Proxy sent no events"))?
                        {
                            EchoEvent::EchoMinimalEvent {} => (),
                            _ => panic!("Unexpected event type"),
                        };
                        drop(app);
                    }
                    control_handle
                        .send_echo_minimal_event()
                        .context("Error responding with event")?;
                }
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
                        if let EchoEvent::EchoEvent { value: response_val } = event_stream
                            .try_next()
                            .await
                            .context("Error getting event response from proxy")?
                            .ok_or_else(|| format_err!("Proxy sent no events"))?
                        {
                            value = response_val;
                        } else {
                            panic!("Unexpected event type");
                        }
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

                EchoRequest::EchoNamedStruct { mut value, forward_to_server, responder } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        value = echo
                            .echo_named_struct(&mut value, "")
                            .await
                            .context("Error calling echo_named_struct on proxy")?;
                        drop(app);
                    }
                    responder.send(&mut value).context("Error responding")?;
                }
                EchoRequest::EchoNamedStructWithError {
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
                            .echo_named_struct_with_error(
                                &mut value,
                                result_err,
                                "",
                                result_variant,
                            )
                            .await
                            .context("Error calling echo_named_struct_with_error on proxy")?;
                        drop(app);
                        responder.send(&mut result).context("Error responding")?;
                    } else {
                        let mut result = if let WantResponse::Err = result_variant {
                            EchoEchoNamedStructWithErrorResult::Err(result_err)
                        } else {
                            EchoEchoNamedStructWithErrorResult::Ok(value)
                        };
                        responder.send(&mut result).context("Error responding")?;
                    }
                }
                EchoRequest::EchoNamedStructNoRetVal {
                    mut value,
                    forward_to_server,
                    control_handle,
                } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        echo.echo_named_struct_no_ret_val(&mut value, "")
                            .context("Error sending echo_named_struct_no_ret_val to proxy")?;
                        let mut event_stream = echo.take_event_stream();
                        if let EchoEvent::OnEchoNamedEvent { value: response_val } = event_stream
                            .try_next()
                            .await
                            .context("Error getting event response from proxy")?
                            .ok_or_else(|| format_err!("Proxy sent no events"))?
                        {
                            value = response_val;
                        } else {
                            panic!("Unexpected event type");
                        }
                        drop(app);
                    }
                    control_handle
                        .send_on_echo_named_event(&mut value)
                        .context("Error responding with event")?;
                }

                EchoRequest::EchoTablePayload { mut payload, responder } => {
                    let forward_to_server = payload.forward_to_server.take();
                    match forward_to_server {
                        Some(forward_to_server) => {
                            let (echo, app) =
                                launch_and_connect_to_echo(launcher, forward_to_server)
                                    .context("Error connecting to proxy")?;
                            let resp = echo
                                .echo_table_payload(payload)
                                .await
                                .context("Error calling echo_table_payload on proxy")?;
                            drop(app);
                            responder.send(resp).context("Error responding")?;
                        }
                        None => {
                            let mut resp = ResponseTable::EMPTY;
                            resp.value = payload.value;
                            responder.send(resp).context("Error responding")?;
                        }
                    }
                }
                EchoRequest::EchoTablePayloadWithError { mut payload, responder } => {
                    let forward_to_server = payload.forward_to_server.take();
                    match forward_to_server {
                        Some(forward_to_server) => {
                            let (echo, app) =
                                launch_and_connect_to_echo(launcher, forward_to_server)
                                    .context("Error connecting to proxy")?;
                            let mut res = echo
                                .echo_table_payload_with_error(payload)
                                .await
                                .context("Error calling echo_table_payload_with_error on proxy")?;
                            responder.send(&mut res).context("Error responding")?;
                            drop(app);
                        }
                        None => {
                            let mut result =
                                if let RespondWith::Err = payload.result_variant.unwrap() {
                                    EchoEchoTablePayloadWithErrorResult::Err(
                                        payload.result_err.unwrap(),
                                    )
                                } else {
                                    let mut resp = ResponseTable::EMPTY;
                                    resp.value = payload.value;
                                    EchoEchoTablePayloadWithErrorResult::Ok(resp)
                                };
                            responder.send(&mut result).context("Error responding")?;
                        }
                    }
                }
                EchoRequest::EchoTablePayloadNoRetVal { mut payload, control_handle } => {
                    let mut resp = ResponseTable::EMPTY;
                    let forward_to_server = payload.forward_to_server.take();
                    match forward_to_server {
                        Some(forward_to_server) => {
                            let (echo, app) =
                                launch_and_connect_to_echo(launcher, forward_to_server)
                                    .context("Error connecting to proxy")?;
                            echo.echo_table_payload_no_ret_val(payload)
                                .context("Error sending echo_table_payload_no_ret_val to proxy")?;
                            let mut event_stream = echo.take_event_stream();
                            if let EchoEvent::OnEchoTablePayloadEvent { payload: response } =
                                event_stream
                                    .try_next()
                                    .await
                                    .context("Error getting event response from proxy")?
                                    .ok_or_else(|| format_err!("Proxy sent no events"))?
                            {
                                resp = response
                            } else {
                                panic!("Unexpected event type");
                            }
                            drop(app);
                        }
                        None => {
                            resp.value = payload.value;
                        }
                    }
                    control_handle
                        .send_on_echo_table_payload_event(resp)
                        .context("Error responding with event")?;
                }
                EchoRequest::EchoTableRequestComposed { mut payload, responder } => {
                    let forward_to_server = payload.forward_to_server.take();
                    match forward_to_server {
                        Some(forward_to_server) => {
                            let (echo, app) =
                                launch_and_connect_to_echo(launcher, forward_to_server)
                                    .context("Error connecting to proxy")?;
                            let mut resp = echo
                                .echo_table_request_composed(payload)
                                .await
                                .context("Error calling echo_table_payload on proxy")?;
                            drop(app);
                            responder.send(&mut resp).context("Error responding")?;
                        }
                        None => {
                            responder
                                .send(&mut SimpleStruct { f1: true, f2: payload.value.unwrap() })
                                .context("Error responding")?;
                        }
                    }
                }

                EchoRequest::EchoUnionPayload { mut payload, responder } => {
                    let forward_to_server = match payload {
                        RequestUnion::Unsigned(ref mut unsigned) => {
                            std::mem::take(&mut unsigned.forward_to_server)
                        }
                        RequestUnion::Signed(ref mut signed) => {
                            std::mem::take(&mut signed.forward_to_server)
                        }
                        RequestUnionUnknown!() => String::new(),
                    };
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        let mut resp = echo
                            .echo_union_payload(&mut payload)
                            .await
                            .context("Error calling echo_union_payload on proxy")?;
                        drop(app);
                        responder.send(&mut resp).context("Error responding")?
                    } else {
                        let mut resp = match payload {
                            RequestUnion::Unsigned(unsigned) => {
                                ResponseUnion::Unsigned(unsigned.value.clone())
                            }
                            RequestUnion::Signed(signed) => {
                                ResponseUnion::Signed(signed.value.clone())
                            }
                            RequestUnionUnknown!() => {
                                return Err(format_err!("Unknown union variant"))
                            }
                        };
                        responder.send(&mut resp).context("Error responding")?;
                    }
                }
                EchoRequest::EchoUnionPayloadWithError { mut payload, responder } => {
                    let forward_to_server = match payload {
                        EchoEchoUnionPayloadWithErrorRequest::Unsigned(ref mut unsigned) => {
                            std::mem::take(&mut unsigned.forward_to_server)
                        }
                        EchoEchoUnionPayloadWithErrorRequest::Signed(ref mut signed) => {
                            std::mem::take(&mut signed.forward_to_server)
                        }
                        EchoEchoUnionPayloadWithErrorRequestUnknown!() => String::new(),
                    };
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        let mut res = echo
                            .echo_union_payload_with_error(&mut payload)
                            .await
                            .context("Error calling echo_union_payload_with_error on proxy")?;
                        responder.send(&mut res).context("Error responding")?;
                        drop(app);
                    } else {
                        let mut result = match payload {
                            EchoEchoUnionPayloadWithErrorRequest::Unsigned(ref mut unsigned) => {
                                if let RespondWith::Err = unsigned.result_variant {
                                    EchoEchoUnionPayloadWithErrorResult::Err(unsigned.result_err)
                                } else {
                                    EchoEchoUnionPayloadWithErrorResult::Ok(
                                        ResponseUnion::Unsigned(unsigned.value.clone()),
                                    )
                                }
                            }
                            EchoEchoUnionPayloadWithErrorRequest::Signed(ref mut signed) => {
                                if let RespondWith::Err = signed.result_variant {
                                    EchoEchoUnionPayloadWithErrorResult::Err(signed.result_err)
                                } else {
                                    EchoEchoUnionPayloadWithErrorResult::Ok(ResponseUnion::Signed(
                                        signed.value.clone(),
                                    ))
                                }
                            }
                            EchoEchoUnionPayloadWithErrorRequestUnknown!() => {
                                return Err(format_err!("Unknown union variant"))
                            }
                        };
                        responder.send(&mut result).context("Error responding")?
                    }
                }
                EchoRequest::EchoUnionPayloadNoRetVal { mut payload, control_handle } => {
                    let forward_to_server = match payload {
                        RequestUnion::Unsigned(ref mut unsigned) => {
                            std::mem::take(&mut unsigned.forward_to_server)
                        }
                        RequestUnion::Signed(ref mut signed) => {
                            std::mem::take(&mut signed.forward_to_server)
                        }
                        RequestUnionUnknown!() => String::new(),
                    };
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        echo.echo_union_payload_no_ret_val(&mut payload)
                            .context("Error sending echo_union_payload_no_ret_val to proxy")?;
                        let mut event_stream = echo.take_event_stream();
                        if let EchoEvent::OnEchoUnionPayloadEvent { payload: mut resp } =
                            event_stream
                                .try_next()
                                .await
                                .context("Error getting event response from proxy")?
                                .ok_or_else(|| format_err!("Proxy sent no events"))?
                        {
                            control_handle
                                .send_on_echo_union_payload_event(&mut resp)
                                .context("Error responding with event")?;
                        } else {
                            panic!("Unexpected event type");
                        }
                        drop(app);
                    } else {
                        let mut resp = match payload {
                            RequestUnion::Unsigned(unsigned) => {
                                ResponseUnion::Unsigned(unsigned.value.clone())
                            }
                            RequestUnion::Signed(signed) => {
                                ResponseUnion::Signed(signed.value.clone())
                            }
                            RequestUnionUnknown!() => {
                                return Err(format_err!("Unknown union variant"))
                            }
                        };
                        control_handle
                            .send_on_echo_union_payload_event(&mut resp)
                            .context("Error responding with event")?;
                    }
                }
                EchoRequest::EchoUnionResponseWithErrorComposed {
                    value,
                    want_absolute_value,
                    forward_to_server,
                    result_err,
                    result_variant,
                    responder,
                } => {
                    if !forward_to_server.is_empty() {
                        let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                            .context("Error connecting to proxy")?;
                        let mut res = echo
                            .echo_union_response_with_error_composed(
                                value,
                                want_absolute_value,
                                "",
                                result_err,
                                result_variant,
                            )
                            .await
                            .context(
                                "Error calling echo_union_response_with_error_composed on proxy",
                            )?;
                        responder.send(&mut res).context("Error responding")?;
                        drop(app);
                    } else {
                        let mut resp = match result_variant {
                            WantResponse::Err => {
                                ComposedEchoUnionResponseWithErrorComposedResult::Err(result_err)
                            }
                            WantResponse::Success => {
                                if want_absolute_value {
                                    ComposedEchoUnionResponseWithErrorComposedResult::Ok(
                                        ComposedEchoUnionResponseWithErrorComposedResponse::Unsigned(
                                            value.abs() as u64,
                                        ),
                                    )
                                } else {
                                    ComposedEchoUnionResponseWithErrorComposedResult::Ok(
                                        ComposedEchoUnionResponseWithErrorComposedResponse::Signed(
                                            value,
                                        ),
                                    )
                                }
                            }
                        };
                        responder.send(&mut resp).context("Error responding")?
                    }
                }
            }
            Ok(())
        })
    };

    let handle_requests_fut = stream
        .err_into() // change error type from fidl::Error to anyhow::Error
        .try_for_each_concurrent(None /* max concurrent requests per connection */, handler);

    handle_requests_fut.await
}

fn main() -> Result<(), Error> {
    let argv: Vec<String> = std::env::args().collect();
    println!("argv={:?}", argv);

    const STACK_SIZE: usize = 1024 * 1024;

    // Create a child thread with a larger stack size to accommodate large structures being built.
    let thread_handle = thread::Builder::new().stack_size(STACK_SIZE).spawn(run_test)?;

    thread_handle.join().expect("Failed to join test thread")
}

fn run_test() -> Result<(), Error> {
    let mut executor = fasync::LocalExecutor::new().context("Error creating executor")?;
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
