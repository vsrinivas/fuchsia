// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_sys::{ComponentControllerEvent, TerminationReason},
    fidl_fuchsia_test_echos::{
        EchoExposedByParentMarker, EchoExposedByParentRequest, EchoExposedByParentRequestStream,
        EchoExposedBySiblingMarker, EchoHiddenByParentMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{connect_to_service, LaunchOptions},
        fuchsia_single_component_package_url,
        server::ServiceFs,
    },
    futures::prelude::*,
    std::{env, io::Write},
    tempfile::TempDir,
};

fn echo_exposed_server(stream: EchoExposedByParentRequestStream) -> impl Future<Output = ()> {
    stream
        .err_into::<anyhow::Error>()
        .try_for_each(|EchoExposedByParentRequest::Echo { value: _, responder }| async move {
            responder.send(42).context("sending response")?;
            Ok(())
        })
        .unwrap_or_else(|e: anyhow::Error| panic!("error running echo server: {:?}", e))
}

const CHILD_URL: &str = fuchsia_single_component_package_url!("fuchsia_component_test_inner");
const ENV_NAME: &str = "fuchsia_component_inner_test_environment";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    env::set_var("RUST_BACKTRACE", "full");

    // Check that all services provided by the parent are available
    let echo = connect_to_service::<EchoExposedByParentMarker>()?;
    assert_eq!(1, echo.echo(1).await?);
    let echo = connect_to_service::<EchoExposedBySiblingMarker>()?;
    assert_eq!(10, echo.echo(5).await?);
    let echo = connect_to_service::<EchoHiddenByParentMarker>()?;
    assert_eq!(2, echo.echo(2).await?);

    println!("connecting to {}", CHILD_URL);

    // expose only one of the echo services, and only ever return
    // 42 from it.
    let mut fs = ServiceFs::new();
    fs.add_fidl_service(|stream| stream);

    let mut launch_options = LaunchOptions::new();
    let tmp_dir = TempDir::new().expect("tempdir creation");
    launch_options
        .add_dir_to_namespace(
            "/dir_exposed_to_inner".to_string(),
            std::fs::File::open(tmp_dir.path()).expect("tempdir open"),
        )
        .expect("adding dir to namespace");
    let tmp_file_path = tmp_dir.path().join("it_works");
    std::fs::File::create(&tmp_file_path)
        .expect("tempdir open")
        .write_all("indeed".as_bytes())
        .expect("write to tempfile");

    let (_new_env_controller, child_app) = fs.launch_component_in_nested_environment_with_options(
        CHILD_URL.to_string(),
        None,
        launch_options,
        ENV_NAME,
    )?;

    // spawn server to respond to child component requests
    fasync::spawn(fs.for_each_concurrent(None, echo_exposed_server));

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
