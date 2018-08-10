// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]
#![feature(futures_api, pin, arbitrary_self_types)]

extern crate failure;
extern crate fidl_fuchsia_bluetooth_control;
extern crate fuchsia_app as app;
#[macro_use]
extern crate fuchsia_async as async;
extern crate fuchsia_bluetooth as bluetooth;
extern crate fuchsia_zircon as zircon;
extern crate futures;
extern crate parking_lot;
extern crate rustyline;

use app::client::connect_to_service;
use bluetooth::types::Status;
use commands::{Cmd, CmdCompleter};
use failure::{Fail, ResultExt};
use failure::Error;
use futures::future;
use fidl_fuchsia_bluetooth_control::{ControlEvent, ControlMarker, ControlProxy};
use futures::{Future, FutureExt, StreamExt};
use futures::TryFutureExt;
use parking_lot::RwLock;
use rustyline::{CompletionType, Config, EditMode, Editor};
use rustyline::error::ReadlineError;
use std::fmt::Write;
use std::sync::Arc;
use types::AdapterInfo;

mod commands;
mod types;

static PROMPT: &'static str = "\x1b[34mbt>\x1b[0m ";

fn get_active_adapter(
    control_svc: Arc<RwLock<ControlProxy>>
) -> impl Future<Output = Result<String, Error>> {
    control_svc
        .read()
        .get_active_adapter_info()
        .map_err(|e| e.context("error getting response").into())
        .and_then(|response| {
            future::ready(Ok(match response {
                None => String::from("No Active Adapter"),
                Some(adapter) => AdapterInfo::from(*adapter).to_string(),
            }))
        })
}

fn get_adapters(
    control_svc: Arc<RwLock<ControlProxy>>
) -> impl Future<Output = Result<String, Error>> {
    control_svc
        .read()
        .get_adapters()
        .map_err(|e| e.context("error getting response").into())
        .and_then(|response| {
            let mut string = String::new();
            future::ready(match response {
                Some(adapters) => {
                    for adapter in adapters {
                        let _ = writeln!(string, "{}", AdapterInfo::from(adapter));
                    }
                    Ok(string)
                }
                None => Ok(String::from("No adapters detected")),
            }
        )})
}

fn start_discovery(
    control_svc: Arc<RwLock<ControlProxy>>
) -> impl Future<Output = Result<String, Error>> {
    control_svc
        .read()
        .request_discovery(true)
        .map_err(|e| e.context("error getting response").into())
        .and_then(|response| future::ready(Ok(Status::from(response).to_string())))
}

fn stop_discovery(
    control_svc: Arc<RwLock<ControlProxy>>
) -> impl Future<Output = Result<String, Error>> {
    control_svc
        .read()
        .request_discovery(false)
        .map_err(|e| e.context("error getting response").into())
        .and_then(|response| future::ready(Ok(Status::from(response).to_string())))
}

fn set_discoverable(discoverable: bool,
    control_svc: Arc<RwLock<ControlProxy>>
) -> impl Future<Output = Result<String, Error>> {
    control_svc
        .read()
        .set_discoverable(discoverable)
        .map_err(|e| e.context("error getting response").into())
        .and_then(|response| future::ready(Ok(Status::from(response).to_string())))
}

fn main() -> Result<(), Error> {
    let config = Config::builder()
        .history_ignore_space(true)
        .completion_type(CompletionType::List)
        .edit_mode(EditMode::Emacs)
        .build();
    let c = CmdCompleter::new();
    let mut rl = Editor::with_config(config);
    rl.set_completer(Some(c));

    let mut exec = async::Executor::new().context("error creating event loop")?;
    let bt_svc = Arc::new(RwLock::new(connect_to_service::<ControlMarker>()
        .context("failed to connect to bluetooth control interface")?));

    let bt_svc_thread = bt_svc.clone();
    let evt_stream = bt_svc_thread.read().take_event_stream();

    // Start listening for events
    async::spawn(
        evt_stream
            .for_each(move |evt| {
                match evt.unwrap() {
                    ControlEvent::OnAdapterUpdated { adapter } => {
                        eprintln!("Adapter {} updated", adapter.identifier);
                    }
                    ControlEvent::OnDeviceUpdated { device } => {
                        eprintln!("Device: {:#?}", device);
                    }
                    ControlEvent::OnDeviceRemoved { identifier } => {
                        eprintln!("Device {} removed", identifier);
                    }
                    _ => eprintln!("Unknown Event"),
                }
                future::ready(())
            })
            .map(|_| ())
    );

    // start the repl
    loop {
        let readline = rl.readline(PROMPT);
        match readline {
            Ok(line) => {
                let cmd = line.parse::<Cmd>();
                unsafe_many_futures!(
                    Output,
                    [
                        StartDiscovery,
                        StopDiscovery,
                        Discoverable,
                        GetAdapters,
                        GetActiveAdapter,
                        Help,
                        Error
                    ]
                );
                let fut = match cmd {
                    Ok(Cmd::StartDiscovery) => {
                        println!("Starting Discovery!");
                        Output::StartDiscovery(start_discovery(bt_svc.clone()))
                    }
                    Ok(Cmd::StopDiscovery) => {
                        println!("Stopping Discovery!");
                        Output::StopDiscovery(stop_discovery(bt_svc.clone()))
                    }
                    Ok(Cmd::Discoverable) => {
                        println!("Becoming discoverable..");
                        Output::Discoverable(set_discoverable(true, bt_svc.clone()))
                    }
                    Ok(Cmd::NotDiscoverable) => {
                        println!("Revoking discoverability..");
                        Output::Discoverable(set_discoverable(false, bt_svc.clone()))
                    }
                    Ok(Cmd::GetAdapters) => Output::GetAdapters(get_adapters(bt_svc.clone())),
                    Ok(Cmd::ActiveAdapter) => {
                        Output::GetActiveAdapter(get_active_adapter(bt_svc.clone()))
                    }
                    Ok(Cmd::Help) => Output::Help(future::ready(Ok(Cmd::help_msg()))),
                    Ok(Cmd::Nothing) => Output::Error(future::ready(Ok(String::from("")))),
                    Err(e) => Output::Error(future::ready(Ok(format!("Error: {:?}", e)))),
                };
                let res = exec.run_singlethreaded(fut)?;
                if res != "" {
                    println!("{}", res);
                }
            }
            Err(ReadlineError::Interrupted) => break,
            Err(_) => break, // empty line
        }
    }
    Ok(())
}
