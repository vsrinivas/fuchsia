// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    failure::{Error, ResultExt},
    fidl::endpoints::{RequestStream, ServiceMarker},
    fidl_fuchsia_sys::{ComponentControllerEvent, TerminationReason},
    fidl_fuchsia_test_echos::{
        EchoExposedByParentMarker,
        EchoExposedByParentRequest,
        EchoExposedByParentRequestStream,
        EchoHiddenByParentMarker,
        EchoHiddenByParentRequest,
        EchoHiddenByParentRequestStream,
    },
    fuchsia_app::{
        fuchsia_single_component_package_url,
        server::ServicesServer,
    },
    fuchsia_async as fasync,
    futures::prelude::*,
};

fn spawn_echo_exposed_server(chan: fasync::Channel) {
    let fut = EchoExposedByParentRequestStream::from_channel(chan)
        .err_into::<failure::Error>()
        .try_for_each(async move |EchoExposedByParentRequest::Echo { value, responder }| {
            responder.send(value).context("sending response")?;
            Ok(())
        })
        .unwrap_or_else(|e: failure::Error|
             panic!("error running echo server: {:?}", e)
        );

    fasync::spawn(fut)
}

fn spawn_echo_hidden_server(chan: fasync::Channel) {
    let fut = EchoHiddenByParentRequestStream::from_channel(chan)
        .err_into::<failure::Error>()
        .try_for_each(async move |EchoHiddenByParentRequest::Echo { value, responder }| {
            responder.send(value).context("sending response")?;
            Ok(())
        })
        .unwrap_or_else(|e: failure::Error|
             panic!("error running echo server: {:?}", e)
        );

    fasync::spawn(fut)
}

const CHILD_URL: &str = fuchsia_single_component_package_url!("fuchsia_app_test_middle_component");
const ENV_NAME: &str = "fuchsia_app_middle_component_test_environment";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    println!("connecting to {}", CHILD_URL);
    let (server, _new_env_controller, child_app) = ServicesServer::new()
        .add_service((EchoExposedByParentMarker::NAME,
                      spawn_echo_exposed_server))
        .add_service((EchoHiddenByParentMarker::NAME,
                      spawn_echo_hidden_server))
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
