// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(futures_api)]

use {
    clap::{App, SubCommand},
    failure::{Error, ResultExt},
    fidl_fuchsia_overnet::{OvernetMarker, OvernetProxy},
    futures::{
        future::lazy,
        prelude::*,
    },
    fuchsia_async::{self as fasync, temp::TempFutureExt},
};

fn app<'a, 'b>() -> App<'a, 'b> {
    App::new("onet")
        .version("0.1.0")
        .about("Overnet debug tool")
        .author("Fuchsia Team")
        .subcommands(vec![
            SubCommand::with_name("ls-peers").about("Lists known peer node ids"),
        ])
}

fn ls_peers(svc: OvernetProxy) -> impl Future<Output = Result<(), Error>> {
    svc.list_peers()
        .map(|peers| {
            for peer in peers {
                println!("PEER: {:?}", peer);
            }
            Ok(())
        })
}

fn dump_error() -> impl Future<Output = Result<(), Error>> {
    lazy(|_| {
        let _ = app().print_help();
        println!("");
        Ok(())
    })
}

fn main() -> Result<(), Error> {
    let args = app().get_matches();

    let mut executor = fasync::Executor::new().context("error creating event loop")?;
    let svc = fuchsia_app::client::connect_to_service::<OvernetMarker>()
        .context("Failed to connect to overnet service")?;

    let fut = match args.subcommand_name() {
        Some("ls-peers") => ls_peers(svc).left_future(),
        _ => dump_error().right_future(),
    };
    executor.run_singlethreaded(fut).map_err(Into::into)
}
