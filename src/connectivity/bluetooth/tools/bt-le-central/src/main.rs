// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context as _, Error},
    fidl_fuchsia_bluetooth_le::{CentralMarker, CentralProxy, ScanFilter},
    fuchsia_async::{
        self as fasync,
        temp::Either::{Left, Right},
    },
    fuchsia_bluetooth::{assigned_numbers::find_service_uuid, error::Error as BTError},
    futures::{future, prelude::*},
    getopts::Options,
};

use crate::central::{connect_peripheral, listen_central_events, CentralState, CentralStatePtr};

mod central;
mod gatt;

fn do_scan(
    appname: &String,
    args: &[String],
    central: &CentralProxy,
) -> (Option<u64>, bool, impl Future<Output = Result<(), Error>>) {
    let mut opts = Options::new();

    opts.optflag("h", "help", "");

    // Options for scan/connection behavior.
    opts.optopt(
        "s",
        "scan-count",
        "number of scan results to return before scanning is stopped",
        "SCAN_COUNT",
    );
    opts.optflag("c", "connect", "connect to the first connectable scan result");

    // Options for filtering scan results.
    opts.optopt("n", "name-filter", "filter by device name", "NAME");
    opts.optopt("u", "uuid-filter", "filter by UUID", "UUID");

    let matches = match opts.parse(args) {
        Ok(m) => m,
        Err(fail) => {
            return (None, false, Left(future::ready(Err(fail.into()))));
        }
    };

    if matches.opt_present("h") {
        let brief = format!(
            "Usage: {} scan (--connect|--scan-count=N) [--name-filter=NAME] \
             [--uuid-filter=UUID]",
            appname
        );
        print!("{}", opts.usage(&brief));
        return (None, false, Left(future::ready(Err(BTError::new("invalid input").into()))));
    }

    let remaining_scan_results: Option<u64> = match matches.opt_str("s") {
        Some(num) => match num.parse() {
            Err(_) | Ok(0) => {
                println!(
                    "{} is not a valid input \
                     - the value must be a positive non-zero number",
                    num
                );
                return (
                    None,
                    false,
                    Left(future::ready(Err(BTError::new("invalid input").into()))),
                );
            }
            Ok(num) => Some(num),
        },
        None => None,
    };

    let connect: bool = matches.opt_present("c");

    if remaining_scan_results.is_some() && connect {
        println!("Cannot use both -s and -c options at the same time");
        return (None, false, Left(future::ready(Err(BTError::new("invalid input").into()))));
    }

    let uuids = match matches.opt_str("u") {
        None => None,
        Some(val) => {
            let uuids = find_service_uuid(&val)
                .map(|sn| sn.number.to_string())
                .or_else(|| if val.len() == 36 { Some(val.clone()) } else { None })
                .map(|uuid| vec![uuid]);

            if uuids.is_none() {
                println!("invalid service UUID: {}", val);
                return (
                    None,
                    false,
                    Left(future::ready(Err(BTError::new("invalid input").into()))),
                );
            }

            uuids
        }
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

    let fut = Right(central.start_scan(filter.as_mut()).map_err(|e| e.into()).and_then(|status| {
        future::ready(match status.error {
            None => Ok(()),
            Some(e) => Err(BTError::from(*e).into()),
        })
    }));

    (remaining_scan_results, connect, fut)
}

async fn do_connect<'a>(state: CentralStatePtr, args: &'a [String]) -> Result<(), Error> {
    if args.len() != 1 {
        println!("connect: peer-id is required");
        return Err(BTError::new("invalid input").into());
    }

    connect_peripheral(&state, args[0].clone()).await
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
    let central_svc = fuchsia_component::client::connect_to_service::<CentralMarker>()
        .context("Failed to connect to BLE Central service")?;

    let state = CentralState::new(central_svc);

    let command = &args[1];
    let fut = async {
        match command.as_str() {
            "scan" => {
                let fut = {
                    let mut central = state.write();
                    let (remaining_scan_results, connect, fut) =
                        do_scan(appname, &args[2..], central.get_svc());
                    central.remaining_scan_results = remaining_scan_results;
                    central.connect = connect;
                    fut
                };
                fut.await?
            }
            "connect" => do_connect(state.clone(), &args[2..]).await?,
            _ => {
                println!("Invalid command: {}", command);
                usage(appname);
                return Err(BTError::new("invalid input").into());
            }
        }

        listen_central_events(state).await;
        Ok(())
    };

    executor.run_singlethreaded(fut)
}
