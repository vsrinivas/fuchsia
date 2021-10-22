// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This component does the following:
//!
//! 1) Makes a call to the `Echo` protocol from the parent, which is served by the test.
//!    This protocol is routed through the core proxy.
//! 2) Serves the `Echo` protocol after #1. The test will invoke this protocol to test that
//!    it can access a protocol routed from appmgr.

use {
    fidl_fidl_examples_routing_echo as fecho, fuchsia_component::client,
    fuchsia_component::server::ServiceFs, futures::prelude::*, log::*,
};

enum IncomingRequest {
    Echo(fecho::EchoRequestStream),
}

#[fuchsia::component]
async fn main() {
    let echo = client::connect_to_protocol_at::<fecho::EchoMarker>("/svc_for_sys").unwrap();
    info!("call echo");
    let out = echo.echo_string(Some("hello")).await.unwrap();
    info!("received echo response");
    assert_eq!(out.unwrap(), "hello");

    info!("serving echo");
    let mut service_fs = ServiceFs::new_local();
    service_fs.dir("svc").add_fidl_service(IncomingRequest::Echo);
    service_fs.take_and_serve_directory_handle().unwrap();
    service_fs
        .for_each_concurrent(None, |request: IncomingRequest| async move {
            match request {
                IncomingRequest::Echo(stream) => handle_echo_request(stream).await,
            }
        })
        .await;
}

async fn handle_echo_request(mut stream: fecho::EchoRequestStream) {
    while let Some(event) = stream.try_next().await.unwrap() {
        info!("received echo request");
        let fecho::EchoRequest::EchoString { value, responder } = event;
        responder.send(value.as_ref().map(|s| &**s)).unwrap();
    }
}
