// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api)]
#![deny(warnings)]

extern crate failure;
extern crate fidl;
extern crate fidl_fuchsia_bluetooth as fidl_bt;
extern crate fidl_fuchsia_bluetooth_gatt as fidl_gatt;
extern crate fidl_fuchsia_bluetooth_le as fidl_ble;
extern crate fuchsia_app as app;
extern crate fuchsia_async as async;
extern crate fuchsia_bluetooth as bt;
extern crate fuchsia_zircon as zx;
extern crate futures;
extern crate getopts;
extern crate parking_lot;

use async::temp::Either::{Left, Right};
use bt::error::Error as BTError;
use failure::{Error, Fail, ResultExt};
use fidl::encoding2::OutOfLine;
use fidl_ble::{CentralMarker, CentralProxy, ScanFilter};
use futures::future;
use futures::prelude::*;
use getopts::Options;

mod common;

use common::central::{listen_central_events, CentralState};

fn do_scan(
    args: &[String], central: &CentralProxy,
) -> (bool, bool, impl Future<Output = Result<(), Error>>) {
    let mut opts = Options::new();

    opts.optflag("h", "help", "");

    // Options for scan/connection behavior.
    opts.optflag("o", "once", "stop scanning after first result");
    opts.optflag(
        "c",
        "connect",
        "connect to the first connectable scan result",
    );

    // Options for filtering scan results.
    opts.optopt("n", "name-filter", "filter by device name", "NAME");
    opts.optopt("u", "uuid-filter", "filter by UUID", "UUID");

    let matches = match opts.parse(args) {
        Ok(m) => m,
        Err(fail) => {
            return (false, false, Left(future::ready(Err(fail.into()))));
        }
    };

    if matches.opt_present("h") {
        let brief = "Usage: ble-central-tool scan [options]";
        print!("{}", opts.usage(&brief));
        return (
            false,
            false,
            Left(future::ready(Err(BTError::new("invalid input").into()))),
        );
    }

    let scan_once: bool = matches.opt_present("o");
    let connect: bool = matches.opt_present("c");

    let uuids = match matches.opt_str("u") {
        None => None,
        Some(val) => Some(vec![match val.len() {
            4 => format!("0000{}-0000-1000-8000-00805F9B34FB", val),
            36 => val,
            _ => {
                println!("invalid service UUID: {}", val);
                return (
                    false,
                    false,
                    Left(future::ready(Err(BTError::new("invalid input").into()))),
                );
            }
        }]),
    };

    let name = matches.opt_str("n");

    let mut filter = if uuids.is_some() || name.is_some() {
        Some(ScanFilter {
            service_uuids: uuids,
            service_data_uuids: None,
            manufacturer_identifier: None,
            connectable: None,
            name_substring: name,
            max_path_loss: None,
        })
    } else {
        None
    };

    let fut = Right(
        central
            .start_scan(filter.as_mut().map(OutOfLine))
            .map_err(|e| e.context("failed to initiate scan").into())
            .and_then(|status| future::ready(match status.error {
                None => Ok(()),
                Some(e) => Err(BTError::from(*e).into()),
            })),
    );

    (scan_once, connect, fut)
}

fn do_connect(args: &[String], central: &CentralProxy)
    -> impl Future<Output = Result<(), Error>>
{
    if args.len() != 1 {
        println!("connect: peer-id is required");
        return Left(future::ready(Err(BTError::new("invalid input").into())));
    }

    let (_, server_end) = match fidl::endpoints2::create_endpoints() {
        Err(e) => {
            return Left(future::ready(Err(e.into())));
        }
        Ok(x) => x,
    };

    Right(
        central
            .connect_peripheral(&mut args[0].clone(), server_end)
            .map_err(|e| e.context("failed to connect to peripheral").into())
            .and_then(|status| future::ready(match status.error {
                None => Ok(()),
                Some(e) => Err(BTError::from(*e).into()),
            })),
    )
}

fn usage(appname: &str) -> () {
    eprintln!(
        "usage: {} <command>
commands:
  scan: Scan for nearby devices and optionally connect to \
         them (pass -h for additional usage)
  connect: Connect to a peer using its ID",
        appname
    );
}

fn main() -> Result<(), Error> {
    let args: Vec<String> = std::env::args().collect();
    let appname = &args[0];

    if args.len() < 2 {
        usage(appname);
        return Ok(());
    }

    let mut executor = async::Executor::new().context("error creating event loop")?;
    let central_svc = app::client::connect_to_service::<CentralMarker>()
        .context("Failed to connect to BLE Central service")?;

    let state = CentralState::new(central_svc);

    let command = &args[1];
    let command_fut = match command.as_str() {
        "scan" => {
            let mut central = state.write();
            let (scan_once, connect, fut) = do_scan(&args[2..], central.get_svc());
            central.scan_once = scan_once;
            central.connect = connect;
            Left(fut)
        }
        "connect" => Right(do_connect(&args[2..], state.read().get_svc())),
        _ => {
            println!("Invalid command: {}", command);
            usage(appname);
            return Err(BTError::new("invalid input").into());
        }
    };

    let event_fut = listen_central_events(state);
    let fut = command_fut.and_then(|_| event_fut.map(Ok));
    executor.run_singlethreaded(fut).map_err(Into::into)
}
