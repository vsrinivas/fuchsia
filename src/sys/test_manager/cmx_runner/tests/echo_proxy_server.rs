// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fidl_examples_routing_echo::{self as fecho, EchoRequest, EchoRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_component::server::ServiceFs,
    futures::prelude::*,
    tracing::info,
};

async fn run_echo_server(mut stream: EchoRequestStream) -> Result<(), Error> {
    let echo = connect_to_protocol::<fecho::EchoMarker>().expect("error connecting to echo");

    while let Some(EchoRequest::EchoString { value, responder }) =
        stream.try_next().await.context("error running echo server")?
    {
        info!("proxy echo request: {:?}", value);
        let out =
            echo.echo_string(value.as_ref().map(|s| s.as_str())).await.expect("echo_string failed");
        responder.send(out.as_ref().map(|s| s.as_str())).context("error sending response")?;
        info!("echo proxy response '{:?}' sent successfully", out);
    }
    Ok(())
}

#[fuchsia::component]
async fn main() -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::Task::local(async move {
            run_echo_server(stream).await.expect("failed to start echo server.")
        })
        .detach();
    });

    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}
