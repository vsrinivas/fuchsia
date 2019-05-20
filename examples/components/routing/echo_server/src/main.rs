// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use {
    failure::{Error, ResultExt},
    fidl_fidl_examples_routing_echo as fecho, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{StreamExt, TryStreamExt},
};

fn main() -> Result<(), Error> {
    let mut executor = fasync::Executor::new().context("error creating executor")?;
    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(move |stream| {
        fasync::spawn_local(
            async move { await!(run_echo_service(stream)).expect("failed to run echo service") },
        );
    });
    fs.take_and_serve_directory_handle()?;
    executor.run_singlethreaded(fs.collect::<()>());
    Ok(())
}

async fn run_echo_service(mut stream: fecho::EchoRequestStream) -> Result<(), Error> {
    while let Some(event) = await!(stream.try_next())? {
        let fecho::EchoRequest::EchoString { value, responder } = event;
        responder.send(value.as_ref().map(|s| &**s)).context("failed to send")?;
    }
    Ok(())
}
