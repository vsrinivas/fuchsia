// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_sys::{ComponentControllerEvent, TerminationReason},
    fidl_fuchsia_test_echos::{
        EchoExposedByParentRequest,
        EchoExposedByParentRequestStream,
        EchoHiddenByParentRequest,
        EchoHiddenByParentRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_component::{
        fuchsia_single_component_package_url,
        server::ServiceFs,
    },
    futures::prelude::*,
};

fn echo_exposed_server(stream: EchoExposedByParentRequestStream) -> impl Future<Output = ()> {
    stream
        .err_into::<failure::Error>()
        .try_for_each(async move |EchoExposedByParentRequest::Echo { value, responder }| {
            responder.send(value).context("sending response")?;
            Ok(())
        })
        .unwrap_or_else(|e: failure::Error| panic!("error running echo server: {:?}", e))
}

fn echo_hidden_server(stream: EchoHiddenByParentRequestStream) -> impl Future<Output = ()> {
    stream
        .err_into::<failure::Error>()
        .try_for_each(async move |EchoHiddenByParentRequest::Echo { value, responder }| {
            responder.send(value).context("sending response")?;
            Ok(())
        })
        .unwrap_or_else(|e: failure::Error| panic!("error running echo server: {:?}", e))
}

const CHILD_URL: &str = fuchsia_single_component_package_url!("fuchsia_component_test_middle");
const ENV_NAME: &str = "fuchsia_component_middle_test_environment";

enum Services {
    Exposed(EchoExposedByParentRequestStream),
    Hidden(EchoHiddenByParentRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    println!("connecting to {}", CHILD_URL);
    let mut fs = ServiceFs::new();
    fs
        .add_fidl_service(Services::Exposed)
        .add_fidl_service(Services::Hidden);
    let (_new_env_controller, child_app) =
        fs.launch_component_in_nested_environment(CHILD_URL.to_string(), None, ENV_NAME)?;

    // spawn server to respond to child component requests
    fasync::spawn(fs.for_each_concurrent(None, |req| async {
        match req {
            Services::Exposed(stream) => await!(echo_exposed_server(stream)),
            Services::Hidden(stream) => await!(echo_hidden_server(stream)),
        }
    }));

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
