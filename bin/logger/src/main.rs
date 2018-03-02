// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(const_size_of)]

extern crate byteorder;
extern crate failure;
extern crate fidl;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zircon;
#[macro_use]
extern crate futures;
extern crate libc;
extern crate parking_lot;

use failure::{Error, ResultExt};
use app::server::ServicesServer;
use futures::future::ok as fok;

extern crate garnet_public_lib_logger_fidl;
use garnet_public_lib_logger_fidl::Log;

pub mod logger;

struct LogManager {}

impl Log::Server for LogManager {
    type Connect = fidl::ServerImmediate<()>;
    fn connect(&mut self, _socket: ::zircon::Socket) -> Self::Connect {
        // Not implemented
        return fok(());
    }
}

fn main() {
    if let Err(e) = main_wrapper() {
        eprintln!("LoggerService: Error: {:?}", e);
    }
}

fn main_wrapper() -> Result<(), Error> {
    let mut executor = async::Executor::new().context("unable to create executor")?;

    let server = ServicesServer::new()
        .add_service(move || {
            let ls = LogManager {};
            Log::Dispatcher(ls)
        })
        .start()
        .map_err(|e| e.context("error starting service server"))?;

    Ok(executor.run(server, /* threads */ 2).context("running server")?)
}
