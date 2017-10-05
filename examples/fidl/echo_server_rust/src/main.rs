// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate fidl;
extern crate fuchsia_app;
extern crate futures;
extern crate tokio_core;

// Include the generated FIDL bindings for the `Echo` service.
extern crate garnet_examples_fidl_services;
use garnet_examples_fidl_services::Echo;

use fuchsia_app::server::{Server, ServiceProviderServer};
use futures::future;
use tokio_core::reactor;

// `EchoServer` is a zero-sized type that implements the `Echo::Server` trait.
struct EchoServer;

impl Echo::Server for EchoServer {

    type EchoString = future::FutureResult<Option<String>, fidl::CloseChannel>;

    fn echo_string(&mut self, value: Option<String>) -> Self::EchoString
    {
        future::ok(value)
    }
}

fn main() {
    // Create a new reactor core for handling IO events.
    let mut core = reactor::Core::new().expect("Unable to create core");
    let handle = core.handle();

    // Create a service provider which will provide a new EchoServer
    // instance upon receiving a `connect_to_service` request.
    let service_provider =
        ServiceProviderServer::new(&handle)
            .add_service(|| Echo::Dispatcher(EchoServer));

    let server = Server::new_outgoing(service_provider, &handle)
                    .expect("Unable to create FIDL service");

    // Run the server on the reactor core.
    // The server is an empty `Future` that can return `fidl::Error`.
    if let Err(e) = core.run(server) {
        println!("Error running server: {:?}", e);
    }
}
