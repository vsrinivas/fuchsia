// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api)]

use {
    failure::{Error, ResultExt},
    fidl::endpoints::{RequestStream, ServiceMarker},
    fidl_fuchsia_sys::{ComponentControllerEvent, TerminationReason},
    fidl_fuchsia_test_echos::{
        EchoExposedByParentMarker,
        EchoExposedByParentRequest,
        EchoExposedByParentRequestStream,
        EchoHiddenByParentMarker,
    },
    fuchsia_app::{
        client::connect_to_service,
        fuchsia_single_component_package_url,
        server::ServicesServer,
    },
    fuchsia_async as fasync,
    futures::prelude::*,
};

fn spawn_echo_exposed_server(chan: fasync::Channel) {
    let fut = EchoExposedByParentRequestStream::from_channel(chan)
        .err_into::<failure::Error>()
        .try_for_each(async move |EchoExposedByParentRequest::Echo { value: _, responder }| {
            responder.send(42).context("sending response")?;
            Ok(())
        })
        .unwrap_or_else(|e: failure::Error|
             panic!("error running echo server: {:?}", e)
        );

    fasync::spawn(fut)
}

const CHILD_URL: &str = fuchsia_single_component_package_url!("fuchsia_app_test_inner_component");
const ENV_NAME: &str = "fuchsia_app_inner_component_test_environment";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Check that both services provided by the parent are available
    let echo = connect_to_service::<EchoExposedByParentMarker>()?;
    assert_eq!(1, await!(echo.echo(1))?);
    let echo = connect_to_service::<EchoHiddenByParentMarker>()?;
    assert_eq!(2, await!(echo.echo(2))?);

    println!("connecting to {}", CHILD_URL);
    let (server, _new_env_controller, child_app) = ServicesServer::new()
        // expose only one of the echo services, and only ever return
        // 42 from it.
        .add_service((EchoExposedByParentMarker::NAME,
                      spawn_echo_exposed_server))
        .launch_component_in_nested_environment(CHILD_URL.to_string(), None, ENV_NAME)?;

    fasync::spawn(server.unwrap_or_else(|e| panic!("error providing services: {:?}", e)));

    let mut component_stream = child_app.controller().take_event_stream();
    match await!(component_stream.next())
        .expect("component event stream ended before termination event")?
    {
        ComponentControllerEvent::OnDirectoryReady {} =>
            panic!("middle component unexpectedly exposed a directory"),
        ComponentControllerEvent::OnTerminated {
            return_code: 0,
            termination_reason: TerminationReason::Exited,
        } => {},
        ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
            panic!("Unexpected exit condition: {:?}", (return_code, termination_reason))
        }
    }

    Ok(())
}
