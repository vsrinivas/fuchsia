// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_bluetooth::PeerId as FidlPeerId,
    fidl_fuchsia_bluetooth_control::{
        ControlEvent, ControlEventStream, ControlMarker, ControlProxy, PairingOptions,
        PairingSecurityLevel, TechnologyType,
    },
    fuchsia_async::{self as fasync, futures::select},
    fuchsia_bluetooth::types::{AdapterInfo, Peer, Status},
    fuchsia_component::client::connect_to_service,
    futures::{
        channel::mpsc::{channel, SendError},
        FutureExt, Sink, SinkExt, Stream, StreamExt, TryFutureExt, TryStreamExt,
    },
    parking_lot::Mutex,
    pin_utils::pin_mut,
    regex::Regex,
    rustyline::{error::ReadlineError, CompletionType, Config, EditMode, Editor},
    std::{
        cmp::Ordering, collections::HashMap, convert::TryFrom, fmt::Write, iter::FromIterator,
        sync::Arc, thread,
    },
};

use crate::{
    commands::{Cmd, CmdHelper, ReplControl},
    types::{DeviceClass, MajorClass, MinorClass, TryInto},
};

mod commands;
mod types;

static PROMPT: &str = "\x1b[34mbt>\x1b[0m ";
/// Escape code to clear the pty line on which the cursor is located.
/// Used when evented output is intermingled with the REPL prompt.
static CLEAR_LINE: &str = "\x1b[2K";

async fn get_active_adapter(control_svc: &ControlProxy) -> Result<String, Error> {
    match control_svc.get_active_adapter_info().await? {
        Some(adapter) => AdapterInfo::try_from(*adapter).map(|a| a.to_string()),
        None => Ok(String::from("No Active Adapter")),
    }
}

async fn get_adapters(control_svc: &ControlProxy) -> Result<String, Error> {
    if let Some(adapters) = control_svc.get_adapters().await? {
        let mut string = String::new();
        for adapter in adapters {
            let _ = writeln!(string, "{}", AdapterInfo::try_from(adapter)?);
        }
        return Ok(string);
    }
    Ok(String::from("No adapters detected"))
}

async fn set_active_adapter<'a>(
    args: &'a [&'a str],
    control_svc: &'a ControlProxy,
) -> Result<String, Error> {
    if args.len() != 1 {
        return Err(format_err!("usage: {}", Cmd::SetActiveAdapter.cmd_help()));
    }
    println!("Setting active adapter");
    // `args[0]` is the identifier of the adapter to make active
    let response = control_svc.set_active_adapter(args[0]).await?;
    if response.error.is_some() {
        Ok(Status::from(response).to_string())
    } else {
        Ok(String::new())
    }
}

async fn set_adapter_name<'a>(
    args: &'a [&'a str],
    control_svc: &'a ControlProxy,
) -> Result<String, Error> {
    if args.len() > 1 {
        return Err(format_err!("usage: {}", Cmd::SetAdapterName.cmd_help()));
    }
    println!("Setting local name of the active adapter");
    // `args[0]` is the value to set as the name of the adapter
    let response = control_svc.set_name(args.get(0).map(|&name| name)).await?;
    if response.error.is_some() {
        Ok(Status::from(response).to_string())
    } else {
        Ok(String::new())
    }
}

/// Set the class of device for the currently active adapter. Arguments are optional, and defaults
/// will be used if arguments aren't provided.
///
/// Returns an error if the input is not recognized as a valid device class .
async fn set_adapter_device_class<'a>(
    args: &'a [&'a str],
    control_svc: &'a ControlProxy,
) -> Result<String, Error> {
    let mut args = args.iter();
    println!("Setting device class of the active adapter");
    let mut cod = DeviceClass {
        major: args.next().map(|arg| arg.try_into()).unwrap_or(Ok(MajorClass::Uncategorized))?,
        minor: args.next().map(|arg| arg.try_into()).unwrap_or(Ok(MinorClass::not_set()))?,
        service: args.try_into()?,
    }
    .into();
    let response = control_svc.set_device_class(&mut cod).await?;
    if response.error.is_some() {
        Ok(Status::from(response).to_string())
    } else {
        Ok(format!("Set device class to 0x{:x}", cod.value))
    }
}

fn match_peer<'a>(pattern: &'a str, peer: &Peer) -> bool {
    let pattern_upper = &pattern.to_uppercase();
    peer.id.to_string().to_uppercase().contains(pattern_upper)
        || peer.address.to_string().to_uppercase().contains(pattern_upper)
        || peer.name.as_ref().map_or(false, |p| p.contains(pattern))
}

/// Order connected peers as greater than unconnected peers and bonded peers greater than unbonded
/// peers.
fn cmp_peers(a: &Peer, b: &Peer) -> Ordering {
    (a.connected, a.bonded).cmp(&(b.connected, b.bonded))
}

fn get_peers<'a>(args: &'a [&'a str], state: &Mutex<State>) -> String {
    let find = match args.len() {
        0 => "",
        1 => args[0],
        _ => return format!("usage: {}", Cmd::GetPeers.cmd_help()),
    };
    let state = state.lock();
    if state.peers.is_empty() {
        return String::from("No known peers");
    }
    let mut peers: Vec<&Peer> = state.peers.values().filter(|p| match_peer(&find, p)).collect();
    peers.sort_by(|a, b| cmp_peers(&*a, &*b));
    let matched = format!("Showing {}/{} peers\n", peers.len(), state.peers.len());
    String::from_iter(std::iter::once(matched).chain(peers.iter().map(|p| p.to_string())))
}

/// Get the string representation of a peer from either an identifier or address
fn get_peer<'a>(args: &'a [&'a str], state: &Mutex<State>) -> String {
    if args.len() != 1 {
        return format!("usage: {}", Cmd::GetPeer.cmd_help());
    }

    to_identifier(state, args[0])
        .and_then(|id| state.lock().peers.get(&id).map(|peer| peer.to_string()))
        .unwrap_or_else(|| String::from("No known peer"))
}

async fn set_discovery(discovery: bool, control_svc: &ControlProxy) -> Result<String, Error> {
    println!("{} Discovery!", if discovery { "Starting" } else { "Stopping" });
    let response = control_svc.request_discovery(discovery).await?;
    if response.error.is_some() {
        Ok(Status::from(response).to_string())
    } else {
        Ok(String::new())
    }
}

// Find the identifier for a `Peer` based on a `key` that is either an identifier or an
// address.
// Returns `None` if the given address does not belong to a known peer.
fn to_identifier(state: &Mutex<State>, key: &str) -> Option<String> {
    // Compile regex inline because it is not ever expected to be a bottleneck
    let address_pattern = Regex::new(r"^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$")
        .expect("Could not compile mac address regex pattern.");
    if address_pattern.is_match(key) {
        state
            .lock()
            .peers
            .values()
            .find(|peer| peer.address.to_string() == key)
            .map(|peer| peer.id.to_string())
    } else {
        Some(key.to_string())
    }
}

async fn connect<'a>(
    args: &'a [&'a str],
    state: &'a Mutex<State>,
    control_svc: &'a ControlProxy,
) -> Result<String, Error> {
    if args.len() != 1 {
        return Ok(format!("usage: {}", Cmd::Connect.cmd_help()));
    }
    // `args[0]` is the identifier of the peer to connect to
    let id = match to_identifier(state, args[0]) {
        Some(id) => id,
        None => return Ok(format!("Unable to connect: Unknown address {}", args[0])),
    };
    let response = control_svc.connect(&id).await?;
    if response.error.is_some() {
        Ok(Status::from(response).to_string())
    } else {
        Ok(String::new())
    }
}

fn parse_disconnect<'a>(args: &'a [&'a str], state: &'a Mutex<State>) -> Result<String, String> {
    if args.len() != 1 {
        return Err(format!("usage: {}", Cmd::Disconnect.cmd_help()));
    }
    // `args[0]` is the identifier of the peer to connect to
    let id = match to_identifier(state, args[0]) {
        Some(id) => id,
        None => return Err(format!("Unable to disconnect: Unknown address {}", args[0])),
    };
    Ok(id)
}

async fn handle_disconnect(id: String, control_svc: &ControlProxy) -> Result<String, Error> {
    let response = control_svc.disconnect(&id).await?;
    if response.error.is_some() {
        Ok(Status::from(response).to_string())
    } else {
        Ok(String::new())
    }
}

async fn disconnect<'a>(
    args: &'a [&'a str],
    state: &'a Mutex<State>,
    control_svc: &'a ControlProxy,
) -> Result<String, Error> {
    match parse_disconnect(args, state) {
        Ok(id) => handle_disconnect(id, control_svc).await,
        Err(msg) => Ok(msg),
    }
}

fn parse_pairing_security_level(level: &str) -> Result<PairingSecurityLevel, String> {
    match level.to_ascii_uppercase().as_str() {
        "AUTH" => Ok(PairingSecurityLevel::Authenticated),
        "ENC" => Ok(PairingSecurityLevel::Encrypted),
        _ => {
            return Err(
                "Unable to pair: security level must be either \"AUTH\" or \"ENC\"".to_string()
            )
        }
    }
}

fn parse_bondable_mode(mode: &str) -> Result<bool, String> {
    match mode.to_ascii_uppercase().as_str() {
        "T" => Ok(true),
        "F" => Ok(false),
        _ => return Err("Bondable mode must be either \"T\" or \"F\"".to_string()),
    }
}

fn parse_pairing_transport(transport: &str) -> Result<TechnologyType, String> {
    match transport.to_ascii_uppercase().as_str() {
        "BREDR" | "CLASSIC" => Ok(TechnologyType::Classic),
        "LE" => Ok(TechnologyType::LowEnergy),
        _ => {
            return Err("If present, transport must be \"BREDR\"/\"CLASSIC\" or \"LE\"".to_string())
        }
    }
}

fn parse_pair(args: &[&str], state: &Mutex<State>) -> Result<(FidlPeerId, PairingOptions), String> {
    if args.len() < 3 || args.len() > 4 {
        return Err(format!("usage: {}", Cmd::Pair.cmd_help()));
    }
    // `args[0]` is the identifier of the peer to connect to
    let peer_id = match to_identifier(state, args[0]).map(|id| u64::from_str_radix(&id, 16)) {
        Some(Ok(value)) => FidlPeerId { value },
        Some(Err(e)) => return Err(format!("Unable to pair - invalid peer address: {:?}", e)),
        None => return Err(format!("Unable to pair: Unknown address {}", args[0])),
    };
    let le_security_level = Some(parse_pairing_security_level(args[1])?);
    // `args[2]` is the requested bonding preference of the pairing
    let bondable_mode = parse_bondable_mode(args[2])?;
    // if `args[3]` is present, it corresponds to the connected transport over which to pair
    let transport = if args.len() == 4 { Some(parse_pairing_transport(args[3])?) } else { None };
    Ok((
        peer_id,
        PairingOptions { le_security_level, non_bondable: Some(!bondable_mode), transport },
    ))
}

async fn handle_pair(
    mut peer_id: FidlPeerId,
    pairing_opts: PairingOptions,
    control_svc: &ControlProxy,
) -> Result<String, Error> {
    let response = control_svc.pair(&mut peer_id, pairing_opts).await?;
    if response.error.is_some() {
        Ok(Status::from(response).to_string())
    } else {
        Ok(String::new())
    }
}

async fn pair(
    args: &[&str],
    state: &Mutex<State>,
    control_svc: &ControlProxy,
) -> Result<String, Error> {
    match parse_pair(args, state) {
        Ok((peer_id, pairing_opts)) => handle_pair(peer_id, pairing_opts, control_svc).await,
        Err(e) => Ok(e),
    }
}

async fn forget<'a>(
    args: &'a [&'a str],
    state: &'a Mutex<State>,
    control_svc: &'a ControlProxy,
) -> Result<String, Error> {
    if args.len() != 1 {
        return Ok(format!("usage: {}", Cmd::Forget.cmd_help()));
    }
    // `args[0]` is the identifier of the remote device to connect to
    let id = match to_identifier(state, args[0]) {
        Some(id) => id,
        None => return Ok(format!("Unable to forget: Unknown address {}", args[0])),
    };
    let response = control_svc.forget(&id).await?;
    if response.error.is_some() {
        Ok(Status::from(response).to_string())
    } else {
        println!("Peer has been removed");
        Ok(String::new())
    }
}

async fn set_discoverable(discoverable: bool, control_svc: &ControlProxy) -> Result<String, Error> {
    if discoverable {
        println!("Becoming discoverable..");
    } else {
        println!("Revoking discoverability..");
    }
    let response = control_svc.set_discoverable(discoverable).await?;
    if response.error.is_some() {
        Ok(Status::from(response).to_string())
    } else {
        Ok(String::new())
    }
}

/// Listen on the control event channel for new events. Track state and print output where
/// appropriate.
async fn run_listeners(mut stream: ControlEventStream, state: &Mutex<State>) -> Result<(), Error> {
    while let Some(evt) = stream.try_next().await? {
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
                let peer = Peer::try_from(device).context("Malformed FIDL peer")?;
                print_peer_state_updates(&state.lock(), &peer);
                state.lock().peers.insert(peer.id.to_string(), peer);
            }
            ControlEvent::OnDeviceRemoved { identifier } => {
                state.lock().peers.remove(&identifier);
            }
        }
    }
    Ok(())
}

fn print_peer_state_updates(state: &State, peer: &Peer) {
    if let Some(msg) = peer_state_updates(state, peer) {
        println!("{} {} {}", peer.id, peer.address, msg)
    }
}

fn peer_state_updates(state: &State, peer: &Peer) -> Option<String> {
    let previous = state.peers.get(&peer.id.to_string());
    let was_connected = previous.map_or(false, |p| p.connected);
    let was_bonded = previous.map_or(false, |p| p.bonded);

    let conn_str = match (was_connected, peer.connected) {
        (false, true) => Some("[connected]"),
        (true, false) => Some("[disconnected]"),
        _ => None,
    };
    let bond_str = match (was_bonded, peer.bonded) {
        (false, true) => Some("[bonded]"),
        (true, false) => Some("[unbonded]"),
        _ => None,
    };
    match (conn_str, bond_str) {
        (Some(a), Some(b)) => Some(format!("{} {}", a, b)),
        (Some(a), None) => Some(a.to_string()),
        (None, Some(b)) => Some(b.to_string()),
        (None, None) => None,
    }
}

/// Tracks all state local to the command line tool.
pub struct State {
    pub peers: HashMap<String, Peer>,
}

impl State {
    pub fn new(
        devs: Vec<fidl_fuchsia_bluetooth_control::RemoteDevice>,
    ) -> Result<Arc<Mutex<State>>, Error> {
        use std::convert::TryInto;

        let mut peers = HashMap::new();
        for d in devs {
            peers.insert(d.identifier.clone(), d.try_into()?);
        }

        Ok(Arc::new(Mutex::new(State { peers })))
    }
}

async fn parse_and_handle_cmd(
    bt_svc: &ControlProxy,
    state: Arc<Mutex<State>>,
    line: String,
) -> Result<ReplControl, Error> {
    match parse_cmd(line) {
        ParseResult::Valid((cmd, args)) => handle_cmd(bt_svc, state, cmd, args).await,
        ParseResult::Empty => Ok(ReplControl::Continue),
        ParseResult::Error(err) => {
            println!("{}", err);
            Ok(ReplControl::Continue)
        }
    }
}

enum ParseResult<T> {
    Valid(T),
    Empty,
    Error(String),
}

/// Parse a single raw input command from a user into the command type and argument list
fn parse_cmd(line: String) -> ParseResult<(Cmd, Vec<String>)> {
    let components: Vec<_> = line.trim().split_whitespace().collect();
    match components.split_first() {
        Some((raw_cmd, args)) => match raw_cmd.parse() {
            Ok(cmd) => {
                let args = args.into_iter().map(|s| s.to_string()).collect();
                ParseResult::Valid((cmd, args))
            }
            Err(_) => ParseResult::Error(format!("\"{}\" is not a valid command", raw_cmd)),
        },
        None => ParseResult::Empty,
    }
}

/// Handle a single raw input command from a user and indicate whether the command should
/// result in continuation or breaking of the read evaluate print loop.
async fn handle_cmd(
    bt_svc: &ControlProxy,
    state: Arc<Mutex<State>>,
    cmd: Cmd,
    args: Vec<String>,
) -> Result<ReplControl, Error> {
    let args: Vec<&str> = args.iter().map(|s| s.as_str()).collect();
    let args: &[&str] = &*args;
    let res = match cmd {
        Cmd::Connect => connect(args, &state, &bt_svc).await,
        Cmd::Disconnect => disconnect(args, &state, &bt_svc).await,
        Cmd::Pair => pair(args, &state, &bt_svc).await,
        Cmd::Forget => forget(args, &state, &bt_svc).await,
        Cmd::StartDiscovery => set_discovery(true, &bt_svc).await,
        Cmd::StopDiscovery => set_discovery(false, &bt_svc).await,
        Cmd::Discoverable => set_discoverable(true, &bt_svc).await,
        Cmd::NotDiscoverable => set_discoverable(false, &bt_svc).await,
        Cmd::GetPeers => Ok(get_peers(args, &state)),
        Cmd::GetPeer => Ok(get_peer(args, &state)),
        Cmd::GetAdapters => get_adapters(&bt_svc).await,
        Cmd::SetActiveAdapter => set_active_adapter(args, &bt_svc).await,
        Cmd::SetAdapterName => set_adapter_name(args, &bt_svc).await,
        Cmd::SetAdapterDeviceClass => set_adapter_device_class(args, &bt_svc).await,
        Cmd::ActiveAdapter => get_active_adapter(&bt_svc).await,
        Cmd::Help => Ok(Cmd::help_msg().to_string()),
        Cmd::Exit | Cmd::Quit => return Ok(ReplControl::Break),
    }?;
    if res != "" {
        println!("{}", res);
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
fn cmd_stream(
    state: Arc<Mutex<State>>,
) -> (impl Stream<Item = String>, impl Sink<(), Error = SendError>) {
    // Editor thread and command processing thread must be synchronized so that output
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
                ack_receiver.next().await;
            }
        };
        exec.run_singlethreaded(fut)
    });
    (cmd_receiver, ack_sender)
}

/// REPL execution
async fn run_repl(bt_svc: ControlProxy, state: Arc<Mutex<State>>) -> Result<(), Error> {
    // `cmd_stream` blocks on input in a separate thread and passes commands and acks back to
    // the main thread via async channels.
    let (mut commands, mut acks) = cmd_stream(state.clone());

    while let Some(cmd) = commands.next().await {
        match parse_and_handle_cmd(&bt_svc, state.clone(), cmd).await {
            Ok(ReplControl::Continue) => {} // continue silently
            Ok(ReplControl::Break) => break,
            Err(e) => println!("Error handling command: {}", e),
        }
        acks.send(()).await?;
    }
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let bt_svc = connect_to_service::<ControlMarker>()
        .context("failed to connect to bluetooth control interface")?;
    let evt_stream = bt_svc.take_event_stream();

    let devices = bt_svc
        .get_known_remote_devices()
        .await
        .context("failed to obtain list of remote devices")?;
    let state = State::new(devices)?;
    let repl =
        run_repl(bt_svc, state.clone()).map_err(|e| e.context("REPL failed unexpectedly").into());
    let listeners = run_listeners(evt_stream, &state)
        .map_err(|e| e.context("Failed to subscribe to bluetooth events").into());
    pin_mut!(repl);
    pin_mut!(listeners);
    select! {
        r = repl.fuse() => r,
        l = listeners.fuse() => l,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        anyhow::format_err,
        bt_fidl_mocks::control::ControlMock,
        fidl_fuchsia_bluetooth as fbt, fidl_fuchsia_bluetooth_sys as fsys,
        fuchsia_bluetooth::{
            bt_fidl_status,
            types::{Address, PeerId},
        },
        fuchsia_zircon::{Duration, DurationNum},
        futures::join,
        parking_lot::Mutex,
    };

    fn peer(connected: bool, bonded: bool) -> Peer {
        Peer {
            id: PeerId(0xdeadbeef),
            address: Address::Public([1, 0, 0, 0, 0, 0]),
            technology: fsys::TechnologyType::LowEnergy,
            connected,
            bonded,
            name: None,
            appearance: Some(fbt::Appearance::Phone),
            device_class: None,
            rssi: None,
            tx_power: None,
            le_services: vec![],
            bredr_services: vec![],
        }
    }

    fn named_peer(id: PeerId, address: Address, name: Option<String>) -> Peer {
        Peer {
            id,
            address,
            technology: fsys::TechnologyType::LowEnergy,
            connected: false,
            bonded: false,
            name,
            appearance: Some(fbt::Appearance::Phone),
            device_class: None,
            rssi: None,
            tx_power: None,
            le_services: vec![],
            bredr_services: vec![],
        }
    }

    fn custom_peer(
        id: PeerId,
        address: Address,
        connected: bool,
        bonded: bool,
        rssi: Option<i8>,
    ) -> Peer {
        Peer {
            id,
            address,
            technology: fsys::TechnologyType::LowEnergy,
            connected,
            bonded,
            name: None,
            appearance: Some(fbt::Appearance::Phone),
            device_class: None,
            rssi,
            tx_power: None,
            le_services: vec![],
            bredr_services: vec![],
        }
    }

    fn state_with(p: Peer) -> State {
        let mut peers = HashMap::new();
        peers.insert(p.id.to_string(), p);
        State { peers }
    }

    #[test]
    fn test_match_peer() {
        let nameless_peer =
            named_peer(PeerId(0xabcd), Address::Public([0xAB, 0x89, 0x67, 0x45, 0x23, 0x01]), None);
        let named_peer = named_peer(
            PeerId(0xbeef),
            Address::Public([0x11, 0x00, 0x55, 0x7E, 0xDE, 0xAD]),
            Some("Sapphire".to_string()),
        );

        assert!(match_peer("23", &nameless_peer));
        assert!(!match_peer("23", &named_peer));

        assert!(match_peer("cd", &nameless_peer));
        assert!(match_peer("bee", &named_peer));
        assert!(match_peer("BEE", &named_peer));

        assert!(!match_peer("Sapphire", &nameless_peer));
        assert!(match_peer("Sapphire", &named_peer));

        assert!(match_peer("", &nameless_peer));
        assert!(match_peer("", &named_peer));

        assert!(match_peer("DE", &named_peer));
        assert!(match_peer("de", &named_peer));
    }

    #[test]
    fn test_get_peers() {
        let mut state = State { peers: HashMap::new() };
        state.peers.insert(
            "abcd".to_string(),
            named_peer(PeerId(0xabcd), Address::Public([0xAB, 0x89, 0x67, 0x45, 0x23, 0x01]), None),
        );
        state.peers.insert(
            "beef".to_string(),
            named_peer(
                PeerId(0xbeef),
                Address::Public([0x11, 0x00, 0x55, 0x7E, 0xDE, 0xAD]),
                Some("Sapphire".to_string()),
            ),
        );
        let state = Mutex::new(state);

        // Empty arguments matches everything
        assert!(get_peers(&[], &state).contains("2/2 peers"));
        assert!(get_peers(&[], &state).contains("01:23:45"));
        assert!(get_peers(&[], &state).contains("AD:DE:7E"));

        // No matches prints nothing.
        assert!(get_peers(&["nomatch"], &state).contains("0/2 peers"));
        assert!(!get_peers(&["nomatch"], &state).contains("01:23:45"));
        assert!(!get_peers(&["nomatch"], &state).contains("AD:DE:7E"));

        // We can match either one
        assert!(get_peers(&["01:23"], &state).contains("1/2 peers"));
        assert!(get_peers(&["01:23"], &state).contains("01:23:45"));
        assert!(get_peers(&["abcd"], &state).contains("1/2 peers"));
        assert!(get_peers(&["beef"], &state).contains("AD:DE:7E"));
    }

    #[test]
    fn cmp_peers_correctly_orders_peers() {
        // Sorts connected correctly
        let peer_a =
            custom_peer(PeerId(0xbeef), Address::Public([1, 0, 0, 0, 0, 0]), false, false, None);
        let peer_b =
            custom_peer(PeerId(0xbaaf), Address::Public([2, 0, 0, 0, 0, 0]), true, false, None);
        assert_eq!(cmp_peers(&peer_a, &peer_b), Ordering::Less);

        // Sorts bonded correctly
        let peer_a =
            custom_peer(PeerId(0xbeef), Address::Public([1, 0, 0, 0, 0, 0]), false, false, None);
        let peer_b =
            custom_peer(PeerId(0xbaaf), Address::Public([2, 0, 0, 0, 0, 0]), false, true, None);
        assert_eq!(cmp_peers(&peer_a, &peer_b), Ordering::Less);
    }

    #[test]
    fn test_peer_updates() {
        // Expected Value Table
        // each row lists:
        //   (current peer conn/bond state, new peer conn/bond state, expected string)
        let test_cases = vec![
            // missing
            (None, (true, false), Some("[connected]")),
            (None, (true, true), Some("[connected] [bonded]")),
            (None, (false, true), Some("[bonded]")),
            (None, (false, false), None),
            // disconnected, unbonded
            (Some((false, false)), (true, false), Some("[connected]")),
            (Some((false, false)), (true, true), Some("[connected] [bonded]")),
            (Some((false, false)), (false, true), Some("[bonded]")),
            (Some((false, false)), (false, false), None),
            // connected, unbonded
            (Some((true, false)), (true, false), None),
            (Some((true, false)), (true, true), Some("[bonded]")),
            (Some((true, false)), (false, true), Some("[disconnected] [bonded]")),
            (Some((true, false)), (false, false), Some("[disconnected]")),
            // disconnected, bonded
            (Some((false, true)), (true, false), Some("[connected] [unbonded]")),
            (Some((false, true)), (true, true), Some("[connected]")),
            (Some((false, true)), (false, true), None),
            (Some((false, true)), (false, false), Some("[unbonded]")),
            // connected, bonded
            (Some((true, true)), (true, false), Some("[unbonded]")),
            (Some((true, true)), (true, true), None),
            (Some((true, true)), (false, true), Some("[disconnected]")),
            (Some((true, true)), (false, false), Some("[disconnected] [unbonded]")),
        ];

        for case in test_cases {
            let (prev, (connected, bonded), expected) = case;
            let state = match prev {
                Some((c, b)) => state_with(peer(c, b)),
                None => State { peers: HashMap::new() },
            };
            assert_eq!(
                peer_state_updates(&state, &peer(connected, bonded)),
                expected.map(|s| s.to_string())
            );
        }
    }

    // Test that command lines entered parse correctly to the expected disconnect calls
    #[test]
    fn test_parse_disconnect() {
        let state = Mutex::new(state_with(peer(true, false)));
        let cases = vec![
            // valid peer id
            ("disconnect deadbeef", Ok("deadbeef".to_string())),
            // unknown address
            (
                "disconnect 00:00:00:00:00:00",
                Err("Unable to disconnect: Unknown address 00:00:00:00:00:00".to_string()),
            ),
            // known address
            ("disconnect 00:00:00:00:00:01", Ok("00000000deadbeef".to_string())),
            // no id param
            ("disconnect", Err(format!("usage: {}", Cmd::Disconnect.cmd_help()))),
        ];
        for (line, expected) in cases {
            assert_eq!(parse_disconnect_id(line, &state), expected);
        }
    }

    fn parse_disconnect_id(line: &str, state: &Mutex<State>) -> Result<String, String> {
        let args = match parse_cmd(line.to_string()) {
            ParseResult::Valid((Cmd::Disconnect, args)) => Ok(args),
            ParseResult::Valid((_, _)) => Err("Command is not disconnect"),
            _ => Err("failed"),
        }?;
        let args: Vec<&str> = args.iter().map(|s| s.as_str()).collect();
        let args: &[&str] = &*args;
        parse_disconnect(args, state)
    }

    // Tests that command lines entered parse correctly to the expected pairing calls
    #[test]
    fn test_parse_pairing_security_level() {
        let cases = vec![
            ("Enc", Ok(PairingSecurityLevel::Encrypted)),
            ("AUTH", Ok(PairingSecurityLevel::Authenticated)),
            (
                "SC",
                Err("Unable to pair: security level must be either \"AUTH\" or \"ENC\"".to_string()),
            ),
        ];
        for (input_str, expected) in cases {
            assert_eq!(parse_pairing_security_level(input_str), expected);
        }
    }

    #[test]
    fn test_parse_bondable_mode() {
        let cases = vec![
            ("T", Ok(true)),
            ("f", Ok(false)),
            ("TEST", Err("Bondable mode must be either \"T\" or \"F\"".to_string())),
        ];
        for (input_str, expected) in cases {
            assert_eq!(parse_bondable_mode(input_str), expected);
        }
    }

    #[test]
    fn test_parse_pairing_transport() {
        let cases = vec![
            ("CLAssIC", Ok(TechnologyType::Classic)),
            ("BrEdr", Ok(TechnologyType::Classic)),
            ("LE", Ok(TechnologyType::LowEnergy)),
            (
                "TEST",
                Err("If present, transport must be \"BREDR\"/\"CLASSIC\" or \"LE\"".to_string()),
            ),
        ];
        for (input_str, expected) in cases {
            assert_eq!(parse_pairing_transport(input_str), expected);
        }
    }
    #[test]
    fn test_parse_pair() {
        let state = Mutex::new(state_with(custom_peer(
            PeerId(0xbeef),
            Address::Public([1, 0, 0, 0, 0, 0]),
            true,
            false,
            None,
        )));
        let cases = vec![
            // valid peer id
            (
                "pair beef ENC T LE",
                Ok((
                    FidlPeerId { value: u64::from_str_radix("beef", 16).unwrap() },
                    PairingOptions {
                        le_security_level: Some(PairingSecurityLevel::Encrypted),
                        non_bondable: Some(false),
                        transport: Some(TechnologyType::LowEnergy),
                    },
                )),
            ),
            // known address, no transport
            (
                "pair 00:00:00:00:00:01 AUTH F",
                Ok((
                    FidlPeerId { value: u64::from_str_radix("beef", 16).unwrap() },
                    PairingOptions {
                        le_security_level: Some(PairingSecurityLevel::Authenticated),
                        non_bondable: Some(true),
                        transport: None,
                    },
                )),
            ),
            // no id param
            ("pair", Err(format!("usage: {}", Cmd::Pair.cmd_help()))),
        ];
        for (line, expected) in cases {
            assert_eq!(parse_pair_args(line, &state), expected);
        }
    }

    fn parse_pair_args(
        line: &str,
        state: &Mutex<State>,
    ) -> Result<(FidlPeerId, PairingOptions), String> {
        let args = match parse_cmd(line.to_string()) {
            ParseResult::Valid((Cmd::Pair, args)) => Ok(args),
            ParseResult::Valid((_, _)) => Err("Command is not pair"),
            _ => Err("failed"),
        }?;
        let args: Vec<&str> = args.iter().map(|s| s.as_str()).collect();
        let args: &[&str] = &*args;
        parse_pair(args, state)
    }

    fn timeout() -> Duration {
        20.seconds()
    }

    #[fasync::run_until_stalled(test)]
    async fn test_disconnect() {
        let peer = peer(true, false);
        let peer_id = peer.id.to_string();

        let args = vec![peer_id.as_str()];
        let state = Mutex::new(state_with(peer));
        let (proxy, mut mock) = ControlMock::new(timeout()).expect("failed to create mock");

        let cmd = disconnect(args.as_slice(), &state, &proxy);
        let mock_expect = mock.expect_disconnect(peer_id.clone(), bt_fidl_status!());
        let (result, mock_result) = join!(cmd, mock_expect);

        let _ = mock_result.expect("mock FIDL expectation not satisfied");
        assert_eq!("".to_string(), result.expect("expected success"));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_disconnect_error() {
        let peer = peer(true, false);
        let peer_id = peer.id.to_string();

        let args = vec![peer_id.as_str()];
        let state = Mutex::new(state_with(peer));
        let (proxy, mut mock) = ControlMock::new(timeout()).expect("failed to create mock");

        let error_msg = "oopsy daisy";
        let cmd = disconnect(args.as_slice(), &state, &proxy);
        let mock_expect = mock
            .expect_disconnect(peer_id.clone(), bt_fidl_status!(Failed, format_err!(error_msg)));
        let (result, mock_result) = join!(cmd, mock_expect);

        let _ = mock_result.expect("mock FIDL expectation not satisfied");
        assert!(result.expect("expected a result").contains(error_msg));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_forget() {
        let peer = peer(true, false);
        let peer_id = peer.id.to_string();

        let args = vec![peer_id.as_str()];
        let state = Mutex::new(state_with(peer));
        let (proxy, mut mock) = ControlMock::new(1.second()).expect("failed to create mock");

        let cmd = forget(args.as_slice(), &state, &proxy);
        let mock_expect = mock.expect_forget(peer_id.clone(), bt_fidl_status!());
        let (result, mock_result) = join!(cmd, mock_expect);

        assert!(mock_result.is_ok());
        assert_eq!("".to_string(), result.expect("expected success"));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_forget_error() {
        let peer = peer(true, false);
        let peer_id = peer.id.to_string();

        let args = vec![peer_id.as_str()];
        let state = Mutex::new(state_with(peer));
        let (proxy, mut mock) = ControlMock::new(1.second()).expect("failed to create mock");

        let error_msg = "oopsy daisy";
        let cmd = forget(args.as_slice(), &state, &proxy);
        let mock_expect =
            mock.expect_forget(peer_id.clone(), bt_fidl_status!(Failed, format_err!(error_msg)));
        let (result, mock_result) = join!(cmd, mock_expect);

        assert!(mock_result.is_ok());
        assert!(result.expect("expected a result").contains(error_msg));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_pair() {
        let peer =
            custom_peer(PeerId(0xbeef), Address::Public([1, 0, 0, 0, 0, 0]), true, false, None);
        let peer_id: FidlPeerId = peer.id.into();
        let peer_id_string = peer.id.to_string();
        let pairing_options = PairingOptions {
            le_security_level: Some(PairingSecurityLevel::Encrypted),
            non_bondable: Some(false),
            transport: None,
        };

        let args = vec![peer_id_string.as_str(), "ENC", "T"];
        let state = Mutex::new(state_with(peer));
        let (proxy, mut mock) = ControlMock::new(1.second()).expect("failed to create mock");

        let cmd = pair(args.as_slice(), &state, &proxy);
        let mock_expect = mock.expect_pair(peer_id, pairing_options, bt_fidl_status!());
        let (result, mock_result) = join!(cmd, mock_expect);

        assert!(mock_result.is_ok());
        assert_eq!("".to_string(), result.expect("expected success"));
    }

    #[fasync::run_until_stalled(test)]
    async fn test_pair_error() {
        let peer =
            custom_peer(PeerId(0xbeef), Address::Public([1, 0, 0, 0, 0, 0]), true, false, None);
        let peer_id: FidlPeerId = peer.id.into();
        let peer_id_string = peer.id.to_string();
        let pairing_options = PairingOptions {
            le_security_level: Some(PairingSecurityLevel::Encrypted),
            non_bondable: Some(false),
            transport: None,
        };

        let args = vec![peer_id_string.as_str(), "ENC", "T"];
        let state = Mutex::new(state_with(peer));
        let (proxy, mut mock) = ControlMock::new(1.second()).expect("failed to create mock");

        let error_msg = "oopsy daisy";
        let cmd = pair(args.as_slice(), &state, &proxy);
        let mock_expect = mock.expect_pair(
            peer_id,
            pairing_options,
            bt_fidl_status!(Failed, format_err!(error_msg)),
        );
        let (result, mock_result) = join!(cmd, mock_expect);

        assert!(mock_result.is_ok());
        assert!(result.expect("expected a result").contains(error_msg));
    }
}
