// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro, futures_api, pin)]
#![deny(warnings)]

use {
    crate::{
        commands::{Cmd, CmdHelper, ReplControl},
        types::{AdapterInfo, RemoteDevice},
    },
    failure::{Error, ResultExt},
    fidl_fuchsia_bluetooth_control::{
        ControlEvent,
        ControlEventStream,
        ControlMarker,
        ControlProxy,
    },
    fuchsia_app::client::connect_to_service,
    fuchsia_async::{self as fasync, futures::select},
    fuchsia_bluetooth::types::Status,
    futures::{
        channel::mpsc::{channel, SendError},
        Sink,
        SinkExt,
        Stream,
        StreamExt,
        TryFutureExt,
        TryStreamExt,
    },
    pin_utils::{pin_mut},
    parking_lot::Mutex,
    rustyline::{
        error::ReadlineError,
        CompletionType,
        Config,
        EditMode,
        Editor,
    },
    std::{
        collections::HashMap,
        fmt::Write,
        iter::FromIterator,
        sync::Arc,
        thread,
    },
};


mod commands;
mod types;


static PROMPT: &str = "\x1b[34mbt>\x1b[0m ";
/// Escape code to clear the pty line on which the cursor is located.
/// Used when evented output is intermingled with the REPL prompt.
static CLEAR_LINE: &str = "\x1b[2K";


async fn get_active_adapter(control_svc: &ControlProxy) -> Result<String, Error> {
    if let Some(adapter) = await!(control_svc.get_active_adapter_info())? {
        return Ok(AdapterInfo::from(*adapter).to_string());
    }
    Ok(String::from("No Active Adapter"))
}

async fn get_adapters(control_svc: &ControlProxy) -> Result<String, Error> {
    if let Some(adapters) = await!(control_svc.get_adapters())? {
        let mut string = String::new();
        for adapter in adapters {
            let _ = writeln!(string, "{}", AdapterInfo::from(adapter));
        }
        return Ok(string);
    }
    Ok(String::from("No adapters detected"))
}

fn get_devices(state: &Mutex<State>) -> String {
    let state = state.lock();
    if state.devices.is_empty() {
        String::from("No known remote devices")
    } else {
        String::from_iter(
            state.devices.values().map(|device| device.to_string())
        )
    }
}

fn get_device<'a>(args: &'a [&'a str], state: &Mutex<State>) -> String {
    if args.len() != 1 {
        return format!("usage: {}", Cmd::GetDevice.cmd_help());
    }
    state
        .lock()
        .devices
        .values()
        .find(|device| device.0.address == args[0])
        .map(|device| device.to_string())
        .unwrap_or_else(|| String::from("No known device"))
}

async fn start_discovery(control_svc: &ControlProxy) -> Result<String, Error> {
    let response = await!(control_svc.request_discovery(true))?;
    if response.error.is_some() {
        Ok(Status::from(response).to_string())
    } else {
        Ok(String::new())
    }
}

async fn stop_discovery(control_svc: &ControlProxy) -> Result<String, Error> {
    let response = await!(control_svc.request_discovery(false))?;
    if response.error.is_some() {
        Ok(Status::from(response).to_string())
    } else {
        Ok(String::new())
    }
}

async fn connect<'a>(args: &'a [&'a str], control_svc: &'a ControlProxy) -> Result<String, Error> {
    if args.len() != 1 {
        return Ok(format!("usage: {}", Cmd::Connect.cmd_help()));
    }
    let response = await!(control_svc.connect(args[0]))?;
    if response.error.is_some() {
        Ok(Status::from(response).to_string())
    } else {
        Ok(String::new())
    }
}

async fn set_discoverable(
    discoverable: bool, control_svc: &ControlProxy,
) -> Result<String, Error> {
    let response = await!(control_svc.set_discoverable(discoverable))?;
    if response.error.is_some() {
        Ok(Status::from(response).to_string())
    } else {
        Ok(String::new())
    }
}

/// Listen on the control event channel for new events. Track state and print output where
/// appropriate.
async fn run_listeners(mut stream: ControlEventStream, state: &Mutex<State>)
    -> Result<(), Error>
{
    while let Some(evt) = await!(stream.try_next())? {
        print!("{}", CLEAR_LINE);
        match evt {
            ControlEvent::OnActiveAdapterChanged { adapter: Some(adapter) } => {
                println!("Active adapter set to {}", adapter.address);
            }
            ControlEvent::OnActiveAdapterChanged { adapter: None } => {
                println!("No active adapter");
            }
            ControlEvent::OnAdapterUpdated { adapter } => {
                println!("Adapter {} updated", adapter.address);
            }
            ControlEvent::OnAdapterRemoved { identifier } => {
                println!("Adapter {} removed", identifier);
            }
            ControlEvent::OnDeviceUpdated { device } => {
                let device = RemoteDevice::from(device);
                println!("{}", device.summary());
                state.lock().devices.insert(device.0.identifier.clone(), device);
            }
            ControlEvent::OnDeviceRemoved { identifier } => {
                println!("Device {} removed", identifier);
                state.lock().devices.remove(&identifier);
            }
        }
    }
    Ok(())
}

/// Tracks all state local to the command line tool.
pub struct State {
    pub devices: HashMap<String, RemoteDevice>,
}

impl State {
    pub fn new() -> Arc<Mutex<State>> {
        Arc::new(Mutex::new(State {
            devices: HashMap::new(),
        }))
    }
}

/// Handle a single raw input command from a user and indicate whether the the command should
/// result in continuation or breaking of the read evaluate print loop.
async fn handle_cmd(bt_svc: &ControlProxy, state: Arc<Mutex<State>>, line: String)
    -> Result<ReplControl, Error>
{
    let components: Vec<_> = line.trim().split_whitespace().collect();
    if let Some((raw_cmd, args)) = components.split_first() {
        let cmd = raw_cmd.parse();
        let res = match cmd {
            Ok(Cmd::Connect) => {
                await!(connect(args, &bt_svc))
            }
            Ok(Cmd::StartDiscovery) => {
                println!("Starting Discovery!");
                await!(start_discovery(&bt_svc))
            }
            Ok(Cmd::StopDiscovery) => {
                println!("Stopping Discovery!");
                await!(stop_discovery(&bt_svc))
            }
            Ok(Cmd::Discoverable) => {
                println!("Becoming discoverable..");
                await!(set_discoverable(true, &bt_svc))
            }
            Ok(Cmd::NotDiscoverable) => {
                println!("Revoking discoverability..");
                await!(set_discoverable(false, &bt_svc))
            }
            Ok(Cmd::GetDevices) => Ok(get_devices(&state)),
            Ok(Cmd::GetDevice) => Ok(get_device(args, &state)),
            Ok(Cmd::GetAdapters) => await!(get_adapters(&bt_svc)),
            Ok(Cmd::ActiveAdapter) => await!(get_active_adapter(&bt_svc)),
            Ok(Cmd::Help) => Ok(Cmd::help_msg().to_string()),
            Ok(Cmd::Exit) | Ok(Cmd::Quit) => return Ok(ReplControl::Break),
            Err(_) => Ok(format!("\"{}\" is not a valid command", raw_cmd)),
        }?;
        if res != "" {
            println!("{}", res);
        }
    }

    Ok(ReplControl::Continue)
}

/// Generates a rustyline `Editor` in a separate thread to manage user input. This input is returned
/// as a `Stream` of lines entered by the user.
///
/// The thread exits and the `Stream` is exhausted when an error occurs on stdin or the user
/// sends a ctrl-c or ctrl-d sequence.
///
/// Because rustyline shares control over output to the screen with other parts of the system, a
/// `Sink` is passed to the caller to send acknowledgements that a command has been processed and
/// that rustyline should handle the next line of input.
fn cmd_stream(state: Arc<Mutex<State>>) -> (impl Stream<Item = String>, impl Sink<SinkItem = (), SinkError = SendError>) {

    // Editor thread and command processing thread must be syncronized so that output
    // is printed in the correct order.
    let (mut cmd_sender, cmd_receiver) = channel(512);
    let (ack_sender, mut ack_receiver) = channel(512);

    thread::spawn(move || -> Result<(), Error> {
        let mut exec = fasync::Executor::new().context("error creating readline event loop")?;

        let fut = async {
            let config = Config::builder()
                .auto_add_history(true)
                .history_ignore_space(true)
                .completion_type(CompletionType::List)
                .edit_mode(EditMode::Emacs)
                .build();
            let c = CmdHelper::new(state);
            let mut rl: Editor<CmdHelper> = Editor::with_config(config);
            rl.set_helper(Some(c));
            loop {
                let readline = rl.readline(PROMPT);
                match readline {
                    Ok(line) => {
                        cmd_sender.try_send(line)?;
                    }
                    Err(ReadlineError::Eof) | Err(ReadlineError::Interrupted) => {
                        return Ok(());
                    }
                    Err(e) => {
                        println!("Error: {:?}", e);
                        return Err(e.into());
                    }
                }
                // wait until processing thread is finished evaluating the last command
                // before running the next loop in the repl
                await!(ack_receiver.next());
            }
        };
        exec.run_singlethreaded(fut)
    });
    (cmd_receiver, ack_sender)
}

/// REPL execution
async fn run_repl(bt_svc: ControlProxy, state: Arc<Mutex<State>>) -> Result<(), Error> {
    // `cmd_stream` blocks on input in a seperate thread and passes commands and acks back to
    // the main thread via async channels.
    let (mut commands, mut acks) = cmd_stream(state.clone());
    loop {
        if let Some(cmd) = await!(commands.next()) {
            match await!(handle_cmd(&bt_svc, state.clone(), cmd)) {
                Ok(ReplControl::Continue) => { }
                Ok(ReplControl::Break) => {
                    break;
                }
                Err(e) => {
                    println!("Error handling command: {}", e);
                }
            }
        } else {
            break;
        }
        await!(acks.send(()))?;
    }
    Ok(())
}

fn main() -> Result<(), Error> {
    let state = State::new();
    let mut exec = fasync::Executor::new().context("error creating event loop")?;
    let bt_svc = connect_to_service::<ControlMarker>()
        .context("failed to connect to bluetooth control interface")?;
    let bt_svc_thread = bt_svc.clone();
    let evt_stream = bt_svc_thread.take_event_stream();

    let fut = async {
        let repl = run_repl(bt_svc, state.clone())
            .unwrap_or_else(|e| println!("REPL failed unexpectedly {:?}", e));
        let listeners = run_listeners(evt_stream, &state)
            .unwrap_or_else(|e| println!("Failed to subscribe to bluetooth events {:?}", e));
        pin_mut!(repl);
        pin_mut!(listeners);
        select! {
            repl => (),
            listeners => (),
        };
    };
    exec.run_singlethreaded(fut);
    Ok(())
}
