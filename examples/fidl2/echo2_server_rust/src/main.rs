// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(conservative_impl_trait)]

extern crate fidl;
extern crate failure;
extern crate fuchsia_app as component;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate garnet_examples_fidl2_services_echo2;

use component::server::{ServiceFactories, ServicesServer};
use failure::{Error, ResultExt};
use futures::prelude::*;
use garnet_examples_fidl2_services_echo2::{Echo, EchoImpl};

fn echo_server(chan: async::Channel) -> impl Future<Item = (), Error = Never> {
    EchoImpl {
        state: (),
        echo_string: |_, mut s, res| {
            println!("Received echo request for string {}", s);
            res.send(&mut s)
               .into_future()
               .map(|_| println!("echo response sent successfully"))
               .recover(|e| eprintln!("error sending response: {:?}", e))
       }
    }
    .serve(chan)
    .recover(|e| eprintln!("error running echo server: {:?}", e))
}

struct EchoFactory;
impl ServiceFactories for EchoFactory {
    fn spawn_service(&mut self, service_name: String, channel: async::Channel) {
        println!("Ignoring request for service {} and starting echo server instead", service_name);
        async::spawn(echo_server(channel))
    }
}

fn main() {
    if let Err(e) = main_res() {
        println!("Error: {:?}", e);
    }
}

fn main_res() -> Result<(), Error> {
    let mut executor = async::Executor::new().context("Error creating executor")?;

    let fut = ServicesServer::new_with_factories(EchoFactory).start()
                .context("Error starting echo services server")?;

    executor.run_singlethreaded(fut).context("failed to execute echo future")?;
    Ok(())
}
