// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(async_await, await_macro, futures_api, pin)]

use {
    failure::{format_err, Error, ResultExt},
    fidl::endpoints::{RequestStream, ServiceMarker},
    fidl_fidl_test_compatibility::{
        EchoEvent,
        EchoMarker,
        EchoProxy,
        EchoRequest,
        EchoRequestStream,
    },
    fuchsia_app::{
        client::Launcher,
        client::App,
        server::ServicesServer,
    },
    fuchsia_async as fasync,
    futures::TryStreamExt,
};

fn launch_and_connect_to_echo(launcher: &Launcher, url: String) -> Result<(EchoProxy, App), Error> {
    let app = launcher.launch(url, None)?;
    let echo = app.connect_to_service(EchoMarker)?;
    Ok((echo, app))
}

async fn echo_server(chan: fasync::Channel, launcher: &Launcher) -> Result<(), Error> {
    const CONCURRENT_REQ_LIMIT: Option<usize> = None;
    let handler = move |request| Box::pinned(async move {
        match request {
            EchoRequest::EchoStruct { mut value, forward_to_server, responder } => {
                if !forward_to_server.is_empty() {
                    let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                        .context("Error connecting to proxy")?;
                    value = await!(echo.echo_struct(&mut value, ""))
                        .context("Error calling echo_struct on proxy")?;
                    drop(app);
                }
                responder.send(&mut value)
                    .context("Error responding")?;
            }
            EchoRequest::EchoStructNoRetVal { mut value, forward_to_server, control_handle } => {
                if !forward_to_server.is_empty() {
                    let (echo, app) = launch_and_connect_to_echo(launcher, forward_to_server)
                        .context("Error connecting to proxy")?;
                    echo.echo_struct_no_ret_val(&mut value, "")
                        .context("Error sending echo_struct_no_ret_val to proxy")?;
                    let mut event_stream = echo.take_event_stream();
                    let EchoEvent::EchoEvent { value: response_val } =
                        await!(event_stream.try_next())
                            .context("Error getting event response from proxy")?
                            .ok_or_else(|| format_err!("Proxy sent no events"))?;
                    value = response_val;
                    drop(app);
                }
                control_handle.send_echo_event(&mut value)
                    .context("Error responding with event")?;
            }
        }
        Ok(())
    });

    let handle_requests_fut = EchoRequestStream::from_channel(chan)
        .err_into() // change error type from fidl::Error to failure::Error
        .try_for_each_concurrent(CONCURRENT_REQ_LIMIT, handler);

    await!(handle_requests_fut)
}

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("Error creating executor")?;
    let launcher = Launcher::new().context("Error connecting to application launcher")?;

    let fut = ServicesServer::new()
        .add_service((EchoMarker::NAME, move |chan| {
            let launcher = launcher.clone();
            fasync::spawn(async move {
                if let Err(e) = await!(echo_server(chan, &launcher)) {
                    eprintln!("Closing echo server {:?}", e);
                }
            })
        }))
        .start()
        .context("Error starting compatibility echo ServicesServer")?;

    executor.run_singlethreaded(fut).context("failed to execute echo future")?;
    Ok(())
}
