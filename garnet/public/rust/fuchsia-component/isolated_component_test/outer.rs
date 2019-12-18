// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{Error, ResultExt},
    fidl_fuchsia_sys::{ComponentControllerEvent, TerminationReason},
    fidl_fuchsia_test_echos::{
        EchoExposedByParentRequest, EchoExposedByParentRequestStream, EchoExposedBySiblingMarker,
        EchoHiddenByParentRequest, EchoHiddenByParentRequestStream,
    },
    fuchsia_async as fasync,
    fuchsia_component::{
        client::AppBuilder, fuchsia_single_component_package_url, server::ServiceFs,
    },
    futures::prelude::*,
};

fn echo_exposed_server(stream: EchoExposedByParentRequestStream) -> impl Future<Output = ()> {
    stream
        .err_into::<failure::Error>()
        .try_for_each(|EchoExposedByParentRequest::Echo { value, responder }| async move {
            responder.send(value).context("sending response")?;
            Ok(())
        })
        .unwrap_or_else(|e: failure::Error| panic!("error running echo server: {:?}", e))
}

fn echo_hidden_server(stream: EchoHiddenByParentRequestStream) -> impl Future<Output = ()> {
    stream
        .err_into::<failure::Error>()
        .try_for_each(|EchoHiddenByParentRequest::Echo { value, responder }| async move {
            responder.send(value).context("sending response")?;
            Ok(())
        })
        .unwrap_or_else(|e: failure::Error| panic!("error running echo server: {:?}", e))
}

const CHILD_SIBLING_URL: &str =
    fuchsia_single_component_package_url!("fuchsia_component_test_middle_sibling");
const CHILD_URL: &str = fuchsia_single_component_package_url!("fuchsia_component_test_middle");
const ENV_NAME: &str = "fuchsia_component_middle_test_environment";

enum Services {
    Exposed(EchoExposedByParentRequestStream),
    Hidden(EchoHiddenByParentRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    println!("connecting to {}", CHILD_URL);
    let mut sibling = AppBuilder::new(CHILD_SIBLING_URL);

    let mut fs = ServiceFs::new();
    fs.add_fidl_service(Services::Exposed)
        .add_fidl_service(Services::Hidden)
        .add_proxy_service_to::<EchoExposedBySiblingMarker, _>(sibling.directory_request()?.clone());

    let env = fs.create_nested_environment(ENV_NAME)?;

    // spawn the child components within the newly created environment
    let sibling = sibling.spawn(env.launcher())?;
    let child_app = AppBuilder::new(CHILD_URL).spawn(env.launcher())?;

    // spawn server to respond to child component requests
    fasync::spawn(fs.for_each_concurrent(None, |req| async {
        match req {
            Services::Exposed(stream) => echo_exposed_server(stream).await,
            Services::Hidden(stream) => echo_hidden_server(stream).await,
        }
    }));

    // verify sibling is running by connecting directly to its service
    let multiply_by_two = sibling.connect_to_service::<EchoExposedBySiblingMarker>()?;
    assert_eq!(14, multiply_by_two.echo(7).await?);

    // verify sibling is running by connecting to its service through the nested environment
    let multiply_by_two = env.connect_to_service::<EchoExposedBySiblingMarker>()?;
    assert_eq!(14, multiply_by_two.echo(7).await?);

    // wait for middle component to exit
    let mut component_stream = child_app.controller().take_event_stream();
    match component_stream
        .next()
        .await
        .expect("component event stream ended before termination event")?
    {
        ComponentControllerEvent::OnDirectoryReady {} => {
            panic!("middle component unexpectedly exposed a directory")
        }
        ComponentControllerEvent::OnTerminated {
            return_code: 0,
            termination_reason: TerminationReason::Exited,
        } => {}
        ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
            panic!("Unexpected exit condition: {:?}", (return_code, termination_reason))
        }
    }

    Ok(())
}
