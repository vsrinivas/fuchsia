// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints;
use fidl_fuchsia_bluetooth_le::{CentralMarker, Filter, ScanOptions, ScanResultWatcherMarker};
use fuchsia_async as fasync;
use fuchsia_bluetooth::{
    assigned_numbers,
    error::Error as BTError,
    types::{PeerId, Uuid},
};
use futures::{try_join, TryFutureExt};
use getopts::Options;
use std::str::FromStr;

use crate::central::{CentralState, CentralStatePtr};

mod central;
mod gatt;

async fn do_scan(appname: &String, args: &[String], state: CentralStatePtr) -> Result<(), Error> {
    let mut opts = Options::new();

    let _ = opts.optflag("h", "help", "");

    // Options for scan/connection behavior.
    let _ = opts.optopt(
        "s",
        "scan-count",
        "number of scan results to return before scanning is stopped",
        "SCAN_COUNT",
    );
    let _ = opts.optflag("c", "connect", "connect to the first connectable scan result");

    // Options for filtering scan results.
    let _ = opts.optopt("n", "name-filter", "filter by device name", "NAME");
    let _ = opts.optopt("u", "uuid-filter", "filter by UUID", "UUID");

    let matches = opts.parse(args)?;

    if matches.opt_present("h") {
        let brief = format!(
            "Usage: {} scan (--connect|--scan-count=N) [--name-filter=NAME] \
             [--uuid-filter=UUID]",
            appname
        );
        print!("{}", opts.usage(&brief));
        return Ok(());
    }

    state.write().remaining_scan_results = match matches.opt_str("s") {
        Some(num) => match num.parse() {
            Err(_) | Ok(0) => {
                return Err(format_err!(
                    "{} is not a valid input \
                     - the value must be a positive non-zero number",
                    num
                ));
            }
            Ok(num) => Some(num),
        },
        None => None,
    };

    state.write().connect = matches.opt_present("c");

    if state.read().remaining_scan_results.is_some() && state.read().connect {
        return Err(format_err!("Cannot use both -s and -c options at the same time"));
    }

    let uuid: Option<fidl_fuchsia_bluetooth::Uuid> = match matches.opt_str("u") {
        None => None,
        Some(val) => {
            // Try to find the UUID as an assigned number (name, abbreviation, number), and fall back to
            // constructing a Uuid from a full UUID string.
            let uuid: Option<Uuid> = assigned_numbers::find_service_uuid(&val).map_or_else(
                || Uuid::from_str(val.as_str()).ok(),
                |sn| Some(Uuid::new16(sn.number)),
            );

            if uuid.is_none() {
                return Err(format_err!("invalid service UUID: {}", val));
            }

            uuid.map(Into::into)
        }
    };

    let name = matches.opt_str("n");

    let mut filters = Vec::<Filter>::new();
    if uuid.is_some() {
        filters.push(Filter { service_uuid: uuid, ..Filter::EMPTY });
    }
    if name.is_some() {
        filters.push(Filter { name: name, ..Filter::EMPTY });
    }
    if filters.is_empty() {
        // At least 1 filter must be specified, so pass an empty filter to match everything.
        filters.push(Filter::EMPTY);
    }
    let scan_options = ScanOptions { filters: Some(filters), ..ScanOptions::EMPTY };

    let (result_watcher_client, result_watcher_server) =
        endpoints::create_proxy::<ScanResultWatcherMarker>()
            .context("failed to create ScanResultWatcher endpoints")?;

    let scan_fut = state
        .write()
        .get_svc()
        .scan(scan_options, result_watcher_server)
        .map_err(|e| format_err!("scan error: {:?}", e));

    let watch_fut = central::watch_scan_results(state, result_watcher_client);

    try_join!(scan_fut, watch_fut).map(|_| ())
}

async fn do_connect<'a>(state: CentralStatePtr, args: &'a [String]) -> Result<(), Error> {
    if args.len() < 1 {
        println!("connect: peer-id is required");
        return Err(BTError::new("invalid input").into());
    }

    let mut opts = Options::new();
    let _ = opts.optopt("u", "uuid", "only discover services that match UUID", "UUID");

    let matches = opts.parse(&args[1..])?;

    let possible_uuid = matches.opt_str("u").map(|u| u.parse::<Uuid>());
    let uuid = match possible_uuid {
        None => None,
        Some(Ok(uuid)) => Some(uuid),
        Some(Err(_)) => return Err(BTError::new("invalid UUID").into()),
    };

    let peer_id: PeerId = PeerId::from_str(&args[0]).map_err(|_| format_err!("invalid peer id"))?;

    central::connect(&state, peer_id, uuid).await
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

    let mut executor = fasync::LocalExecutor::new().context("error creating event loop")?;
    let central_svc = fuchsia_component::client::connect_to_protocol::<CentralMarker>()
        .context("Failed to connect to BLE Central service")?;

    let state = CentralState::new(central_svc);

    let command = &args[1];
    let fut = async {
        match command.as_str() {
            "scan" => do_scan(appname, &args[2..], state.clone()).await,
            "connect" => do_connect(state.clone(), &args[2..]).await,
            _ => {
                println!("Invalid command: {}", command);
                usage(appname);
                Err(BTError::new("invalid input").into())
            }
        }
    };

    executor.run_singlethreaded(fut)
}
