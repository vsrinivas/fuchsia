// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(conservative_impl_trait)]

extern crate failure;
extern crate fidl;
extern crate fuchsia_async as async;
extern crate fuchsia_app as app;
extern crate futures;

// The `failure` crate provides an error trait called `Fail`,
// as well as a standard dynamically-dispatched `Error` type.
use failure::{Error, ResultExt};

// Include the generated FIDL bindings for the `Echo` service.
extern crate garnet_examples_fidl_services;
use garnet_examples_fidl_services::Echo;

use app::server::ServicesServer;
use futures::future;

// A function which returns a type implementing the `Echo::Server` trait
fn echo_server() -> impl Echo::Server {
    // Create an instance of the `Impl` struct, which implements `Echo::Server`
    // by delegating each method to one of the struct's fields.
    Echo::Impl {
        // State which is stored between successive calls to handler methods.
        state: (),
        // The implementation of the `echo_string` method of the `Echo::Server`.
        // This function has an inferred type of
        // `FnMut(&mut State, Option<String>) -> impl fidl::ServerFuture<Option<String>>`
        echo_string: |_state, value| {
            // return a `Future` type that resolves to an `Option<String>`
            future::ok(value)
        }
    }
}

fn main() {
    if let Err(e) = main_res() {
        eprintln!("Error running Rust echo server: {:?}", e);
    }
}

fn main_res() -> Result<(), Error> {
    // Create a new executor for running asynchronous tasks.
    let mut executor = async::Executor::new().context("Unable to create executor")?;

    // Create a service provider which will provide a new EchoServer
    // instance upon receiving a `connect_to_service` request.
    let services_server =
        ServicesServer::new()
            .add_service(|| Echo::Dispatcher(echo_server()))
            .start()
            .context("Error configuring services server")?;

    // Run the server on the executor using two worker threads.
    // The server is an empty `Future` that can return `fidl::Error`.
    Ok(executor.run(services_server, /* threads */ 2).context("Error running server")?)
}
