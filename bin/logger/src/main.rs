// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fuchsia_app;
extern crate fuchsia_zircon as zircon;
extern crate futures;
extern crate tokio_core;

use failure::{Error, ResultExt};
use fuchsia_app::server::ServicesServer;
use futures::future::ok as fok;
use tokio_core::reactor;

extern crate garnet_public_lib_logger_fidl;
use garnet_public_lib_logger_fidl::Log;

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
    let mut core = reactor::Core::new().context("unable to create core")?;

    let server = ServicesServer::new()
        .add_service(move || {
            let ls = LogManager {};
            Log::Dispatcher(ls)
        })
        .start(&core.handle())
        .map_err(|e| e.context("error starting service server"))?;

    Ok(core.run(server).context("running server")?)
}
