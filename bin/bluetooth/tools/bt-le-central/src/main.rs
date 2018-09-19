// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(futures_api, async_await, await_macro)]
#![deny(warnings)]

use {
    failure::{Error, Fail, ResultExt},
    fidl::encoding::OutOfLine,
    fidl_fuchsia_bluetooth_le::{CentralMarker, CentralProxy, ScanFilter},
    fuchsia_bluetooth::error::Error as BTError,
    fuchsia_async::{
        self as fasync,
        temp::Either::{Left, Right},
    },
    futures::{
        future,
        prelude::*,
    },
    getopts::Options,
};

mod common;
use self::common::central::{listen_central_events, CentralState};

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

async fn do_connect<'a>(args: &'a [String], central: &'a CentralProxy)
    -> Result<(), Error>
{
    if args.len() != 1 {
        println!("connect: peer-id is required");
        return Err(BTError::new("invalid input").into());
    }

    let (_, server_end) = fidl::endpoints::create_endpoints()?;

    let status = await!(central.connect_peripheral(&args[0], server_end))
        .context("failed to connect to peripheral")?;

    match status.error {
        None => Ok(()),
        Some(e) => Err(BTError::from(*e).into()),
    }
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

    let mut executor = fasync::Executor::new().context("error creating event loop")?;
    let central_svc = fuchsia_app::client::connect_to_service::<CentralMarker>()
        .context("Failed to connect to BLE Central service")?;

    let state = CentralState::new(central_svc);

    let command = &args[1];
    let fut = async {
        match command.as_str() {
            "scan" => {
                let fut = {
                    let mut central = state.write();
                    let (scan_once, connect, fut) = do_scan(&args[2..], central.get_svc());
                    central.scan_once = scan_once;
                    central.connect = connect;
                    fut
                };
                await!(fut)?
            }
            "connect" => {
                let svc = state.read().get_svc().clone();
                await!(do_connect(&args[2..], &svc))?
            },
            _ => {
                println!("Invalid command: {}", command);
                usage(appname);
                return Err(BTError::new("invalid input").into());
            }
        }

        await!(listen_central_events(state));
        Ok(())
    };

    executor.run_singlethreaded(fut)
}
