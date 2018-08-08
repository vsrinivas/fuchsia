// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

extern crate clap;
extern crate failure;
extern crate fidl_fuchsia_overnet;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate futures;

use clap::{App, SubCommand};
use failure::{Error, Fail, ResultExt};
use fidl_fuchsia_overnet::{OvernetMarker, OvernetProxy};
use futures::{future::lazy, prelude::*};

fn app<'a, 'b>() -> App<'a, 'b> {
    App::new("onet")
        .version("0.1.0")
        .about("Overnet debug tool")
        .author("Fuchsia Team")
        .subcommands(vec![
            SubCommand::with_name("ls-peers").about("Lists known peer node ids"),
        ])
}

fn ls_peers(svc: OvernetProxy) -> impl Future<Item = (), Error = Error> {
    svc.list_peers()
        .map(|peers| {
            for peer in peers {
                println!("PEER: {}", peer.id);
            }
        })
        .map_err(|e| e.context("Failed to list peers").into())
}

fn dump_error() -> impl Future<Item = (), Error = Error> {
    lazy(|_| {
        app().print_help();
        println!("");
        Ok(())
    })
}

fn main() -> Result<(), Error> {
    let args = app().get_matches();

    let mut executor = async::Executor::new().context("error creating event loop")?;
    let svc = app::client::connect_to_service::<OvernetMarker>()
        .context("Failed to connect to overnet service")?;

    let fut = match args.subcommand_name() {
        Some("ls-peers") => ls_peers(svc).left_future(),
        _ => dump_error().right_future(),
    };
    executor.run_singlethreaded(fut).map_err(Into::into)
}
