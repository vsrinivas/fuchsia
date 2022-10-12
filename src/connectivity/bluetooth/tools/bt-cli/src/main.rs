// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_bluetooth::{HostId as FidlHostId, PeerId as FidlPeerId},
    fidl_fuchsia_bluetooth_sys::{
        AccessMarker, AccessProxy, BondableMode, HostWatcherMarker, HostWatcherProxy,
        PairingDelegateMarker, PairingMarker, PairingOptions, PairingProxy, PairingSecurityLevel,
        ProcedureTokenProxy, TechnologyType,
    },
    fuchsia_async as fasync,
    fuchsia_bluetooth::types::io_capabilities::{InputCapability, OutputCapability},
    fuchsia_bluetooth::types::{HostId, HostInfo, Peer, PeerId},
    fuchsia_component::client::connect_to_protocol,
    futures::{channel::mpsc, select, FutureExt, Sink, SinkExt, Stream, StreamExt, TryFutureExt},
    pairing_delegate,
    parking_lot::Mutex,
    pin_utils::pin_mut,
    prettytable::{cell, format, row, Row, Table},
    regex::Regex,
    rustyline::{error::ReadlineError, CompletionType, Config, EditMode, Editor},
    std::{
        cmp::Ordering, collections::HashMap, convert::TryFrom, iter::FromIterator, str::FromStr,
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

async fn get_active_host(state: &Mutex<State>) -> Result<String, Error> {
    let state = state.lock();
    let active_iter = state.hosts.iter().find(|&h| h.active);
    match active_iter {
        Some(host) => Ok(host.to_string()),
        None => Ok(String::from("No Active Adapter")),
    }
}

async fn get_hosts(state: &Mutex<State>) -> Result<String, Error> {
    let hosts = &state.lock().hosts;
    if hosts.is_empty() {
        return Ok(String::from("No adapters detected"));
    }
    // Create table of results
    let mut table = Table::new();
    table.set_format(*format::consts::FORMAT_NO_BORDER_LINE_SEPARATOR);
    let _ = table.set_titles(row![
        "HostId",
        "Address",
        "Active",
        "Technology",
        "Name",
        "Discoverable",
        "Discovering",
    ]);
    for host in hosts {
        let _ = table.add_row(row![
            host.id.to_string(),
            format!("{}", host.address),
            host.active.to_string(),
            format!("{:?}", host.technology),
            host.local_name.clone().unwrap_or("(unknown)".to_string()),
            host.discoverable.to_string(),
            host.discovering.to_string(),
        ]);
    }
    Ok(format!("{}", table))
}

async fn set_active_host<'a>(
    args: &'a [&'a str],
    host_svc: &'a HostWatcherProxy,
) -> Result<String, Error> {
    if args.len() != 1 {
        return Err(format_err!("usage: {}", Cmd::SetActiveAdapter.cmd_help()));
    }
    println!("Setting active adapter");
    let host_id = HostId::from_str(args[0])?;
    let mut fidl_host_id: FidlHostId = host_id.into();
    match host_svc.set_active(&mut fidl_host_id).await {
        Ok(_) => Ok(String::new()),
        Err(err) => Ok(format!("Error setting active host: {}", err)),
    }
}

async fn set_local_name<'a>(
    args: &'a [&'a str],
    access_svc: &'a AccessProxy,
) -> Result<String, Error> {
    if args.len() != 1 {
        return Ok(format!("usage: {}", Cmd::SetAdapterName.cmd_help()));
    }
    println!("Setting local name of the active host");
    match access_svc.set_local_name(args[0]) {
        Ok(_) => Ok(String::new()),
        Err(err) => Ok(format!("Error setting local name: {:?}", err)),
    }
}

/// Set the class of device for the currently active adapter. Arguments are optional, and defaults
/// will be used if arguments aren't provided.
///
/// Returns an error if the input is not recognized as a valid device class .
async fn set_device_class<'a>(
    args: &'a [&'a str],
    access_svc: &'a AccessProxy,
) -> Result<String, Error> {
    let mut args = args.iter();
    println!("Setting device class of the active adapter");
    let mut device_class = DeviceClass {
        major: args
            .next()
            .map(|arg| TryInto::try_into(&**arg))
            .unwrap_or(Ok(MajorClass::Uncategorized))?,
        minor: args
            .next()
            .map(|arg| TryInto::try_into(&**arg))
            .unwrap_or(Ok(MinorClass::not_set()))?,
        service: TryInto::try_into(args)?,
    }
    .into();

    match access_svc.set_device_class(&mut device_class) {
        Ok(_) => Ok(format!("Set device class to 0x{:x}", device_class.value)),
        Err(err) => Ok(format!("Error setting device class: {}", err)),
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

fn get_peers<'a>(args: &'a [&'a str], state: &Mutex<State>, full_details: bool) -> String {
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

    if full_details {
        return String::from_iter(
            std::iter::once(matched).chain(peers.iter().map(|p| p.to_string())),
        );
    }

    // Create table of results
    let mut table = Table::new();
    table.set_format(*format::consts::FORMAT_NO_BORDER);
    let _ = table.set_titles(row![
        "PeerId",
        "Address",
        "Technology",
        "Name",
        "Appearance",
        "Connected",
        "Bonded",
    ]);
    for val in peers.into_iter() {
        let _ = table.add_row(peer_to_table_row(val));
    }
    [matched, format!("{}", table)].join("\n")
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

/// Returns basic peer information formatted as a prettytable Row
fn peer_to_table_row(peer: &Peer) -> Row {
    row![
        peer.id.to_string(),
        peer.address.to_string(),
        format! {"{:?}", peer.technology},
        peer.name.as_ref().map_or("".to_string(), |x| format!("{:?}", x)),
        peer.appearance.as_ref().map_or("".to_string(), |x| format!("{:?}", x)),
        peer.connected.to_string(),
        peer.bonded.to_string(),
    ]
}

async fn set_discovery(
    discovery: bool,
    state: &Mutex<State>,
    access_svc: &AccessProxy,
) -> Result<String, Error> {
    println!("{} Discovery!", if discovery { "Starting" } else { "Stopping" });
    if !discovery {
        state.lock().discovery_token = None;
        return Ok(String::new());
    }

    let (token, token_server) = fidl::endpoints::create_proxy()?;
    match access_svc.start_discovery(token_server).await? {
        Ok(_) => {
            state.lock().discovery_token = Some(token);
            Ok(String::new())
        }
        Err(err) => Ok(format!("Discovery error: {:?}", err)),
    }
}

// Find the identifier for a `Peer` based on a `key` that is either an identifier or an
// address.
// Returns `None` if the given address does not belong to a known peer.
fn to_identifier(state: &Mutex<State>, key: &str) -> Option<PeerId> {
    // Compile regex inline because it is not ever expected to be a bottleneck
    let address_pattern = Regex::new(r"^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$")
        .expect("Could not compile mac address regex pattern.");
    if address_pattern.is_match(key) {
        state.lock().peers.values().find(|peer| &peer.address == key).map(|peer| peer.id)
    } else {
        key.parse().ok()
    }
}

async fn connect<'a>(
    args: &'a [&'a str],
    state: &'a Mutex<State>,
    access_proxy: &'a AccessProxy,
    with_pairing: Option<&'a PairingProxy>,
) -> Result<String, Error> {
    if args.len() != 1 {
        return Ok(format!("usage: {}", Cmd::Connect.cmd_help()));
    }
    // `args[0]` is the identifier of the peer to connect to
    let peer_id = match to_identifier(state, args[0]) {
        Some(id) => id,
        None => return Ok(format!("Unable to connect: Unknown address {}", args[0])),
    };
    if let Err(e) = access_proxy.connect(&mut peer_id.into()).await? {
        return Ok(format!("connect error: {:?}", e));
    }

    let pairing = with_pairing
        .map(|p| create_pairing_task(InputCapability::None, OutputCapability::None, &p))
        .transpose()?;
    if let Some((pairing_task, mut recv)) = pairing {
        if let Some((paired_id, paired)) = recv.next().await {
            // If pairing was completed, exit.
            let _ = pairing_task.cancel().await;
            println!(
                "Completed {} pairing for {}.",
                if paired { "successful" } else { "unsuccessful" },
                paired_id
            );
        } else {
            println!("Note: No pairing occurred with {}.", peer_id);
        }
    }
    Ok(String::new())
}

fn parse_disconnect<'a>(args: &'a [&'a str], state: &'a Mutex<State>) -> Result<PeerId, String> {
    if args.len() != 1 {
        return Err(format!("usage: {}", Cmd::Disconnect.cmd_help()));
    }
    // `args[0]` is the identifier of the peer to connect to
    match to_identifier(state, args[0]) {
        Some(id) => Ok(id),
        None => return Err(format!("Unable to disconnect: Unknown address {}", args[0])),
    }
}

async fn handle_disconnect(id: PeerId, access_svc: &AccessProxy) -> Result<String, Error> {
    let response = access_svc.disconnect(&mut id.into()).await?;
    match response {
        Ok(_) => Ok(String::new()),
        Err(err) => Ok(format!("Disconnect error: {:?}", err)),
    }
}

async fn disconnect<'a>(
    args: &'a [&'a str],
    state: &'a Mutex<State>,
    access_svc: &'a AccessProxy,
) -> Result<String, Error> {
    match parse_disconnect(args, state) {
        Ok(id) => handle_disconnect(id, access_svc).await,
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

fn parse_bondable_mode(mode: &str) -> Result<BondableMode, String> {
    match mode.to_ascii_uppercase().as_str() {
        "T" => Ok(BondableMode::Bondable),
        "F" => Ok(BondableMode::NonBondable),
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
    let peer_id = match to_identifier(state, args[0]) {
        Some(id) => id,
        None => return Err(format!("Unable to pair: invalid peer address: {}", args[0])),
    };
    let le_security_level = Some(parse_pairing_security_level(args[1])?);
    // `args[2]` is the requested bonding preference of the pairing
    let bondable_mode = Some(parse_bondable_mode(args[2])?);
    // if `args[3]` is present, it corresponds to the connected transport over which to pair
    let transport = if args.len() == 4 { Some(parse_pairing_transport(args[3])?) } else { None };
    Ok((
        peer_id.into(),
        PairingOptions { le_security_level, bondable_mode, transport, ..PairingOptions::EMPTY },
    ))
}

async fn handle_pair(
    mut peer_id: FidlPeerId,
    pairing_opts: PairingOptions,
    access_svc: &AccessProxy,
) -> Result<String, Error> {
    match access_svc.pair(&mut peer_id, pairing_opts).await? {
        Ok(_) => Ok(String::new()),
        Err(err) => Ok(format!("Pair error: {:?}", err)),
    }
}

async fn pair(
    args: &[&str],
    state: &Mutex<State>,
    access_svc: &AccessProxy,
) -> Result<String, Error> {
    match parse_pair(args, state) {
        Ok((peer_id, pairing_opts)) => handle_pair(peer_id, pairing_opts, access_svc).await,
        Err(e) => Ok(e),
    }
}

// Creates a pairing task with the given input / output capabilities which will prompt for consent
// to the pairing.  The returned task will continue prompting for requests until it is dropped.
fn create_pairing_task(
    input_cap: InputCapability,
    output_cap: OutputCapability,
    pairing_svc: &PairingProxy,
) -> Result<(fasync::Task<()>, mpsc::Receiver<(PeerId, bool)>), Error> {
    let (pairing_delegate_client, delegate_stream) =
        fidl::endpoints::create_request_stream::<PairingDelegateMarker>()?;
    let (sender, recv) = mpsc::channel(0);
    let pairing_delegate_server = pairing_delegate::handle_requests(delegate_stream, sender);

    let _ = pairing_svc.set_pairing_delegate(
        input_cap.into(),
        output_cap.into(),
        pairing_delegate_client,
    );

    let task = fasync::Task::spawn(pairing_delegate_server.map(|res| println!("{res:?}")));
    println!(
        "Pairing delegate setup with input capability {:?} and output capability {:?}.",
        input_cap, output_cap
    );
    Ok((task, recv))
}

async fn allow_pairing(args: &[&str], access_svc: &PairingProxy) -> Result<String, Error> {
    let (input_cap, output_cap) = match args.len() {
        0 => (InputCapability::None, OutputCapability::None),
        2 => (
            InputCapability::from_str(args[0]).map_err(|_| {
                format_err!("unknown input capability: {}", Cmd::AllowPairing.cmd_help())
            })?,
            OutputCapability::from_str(args[1]).map_err(|_| {
                format_err!("unknown output capability: {}", Cmd::AllowPairing.cmd_help())
            })?,
        ),
        _ => return Err(format_err!("usage: {}", Cmd::AllowPairing.cmd_help())),
    };
    let (delegate_task, mut receiver) = create_pairing_task(input_cap, output_cap, access_svc)?;

    if let Some((paired_id, paired)) = receiver.next().await {
        // If pairing was completed, exit.
        let _ = delegate_task.cancel().await;
        return Ok(format!(
            "Completed {} pairing with {}.",
            if paired { "successful" } else { "unsuccessful" },
            paired_id
        ));
    }
    Err(format_err!("Pairing delegate closed without a pairing"))
}

async fn forget<'a>(
    args: &'a [&'a str],
    state: &'a Mutex<State>,
    access_svc: &'a AccessProxy,
) -> Result<String, Error> {
    if args.len() != 1 {
        return Ok(format!("usage: {}", Cmd::Forget.cmd_help()));
    }
    // `args[0]` is the identifier of the remote device to connect to
    let peer_id = match to_identifier(state, args[0]) {
        Some(id) => id,
        None => return Ok(format!("Unable to forget: Unknown address {}", args[0])),
    };
    match access_svc.forget(&mut peer_id.into()).await? {
        Ok(_) => {
            println!("Peer has been removed");
            Ok(String::new())
        }
        Err(err) => Ok(format!("Forget error: {:?}", err)),
    }
}

async fn set_discoverable(
    discoverable: bool,
    access_svc: &AccessProxy,
    state: &Mutex<State>,
) -> Result<String, Error> {
    if discoverable {
        println!("Becoming discoverable..");
        if state.lock().discoverable_token.is_some() {
            return Ok(String::new());
        }
        let (token, token_server) = fidl::endpoints::create_proxy()?;
        match access_svc.make_discoverable(token_server).await? {
            Ok(_) => {
                state.lock().discoverable_token = Some(token);
                Ok(String::new())
            }
            Err(err) => Ok(format!("MakeDiscoverable error: {:?}", err)),
        }
    } else {
        println!("Revoking discoverability..");
        state.lock().discoverable_token = None;
        Ok(String::new())
    }
}

fn print_peer_state_updates(state: &State, peer: &Peer) {
    if let Some(msg) = peer_state_updates(state, peer) {
        println!("{} {} {}", peer.id, peer.address, msg)
    }
}

fn peer_state_updates(state: &State, peer: &Peer) -> Option<String> {
    let previous = state.peers.get(&peer.id);
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
    pub peers: HashMap<PeerId, Peer>,
    pub discoverable_token: Option<ProcedureTokenProxy>,
    pub discovery_token: Option<ProcedureTokenProxy>,
    pub hosts: Vec<HostInfo>,
}

impl State {
    pub fn new() -> State {
        State {
            peers: HashMap::new(),
            discoverable_token: None,
            discovery_token: None,
            hosts: vec![],
        }
    }
}

async fn parse_and_handle_cmd(
    bt_svc: &AccessProxy,
    host_svc: &HostWatcherProxy,
    pairing_svc: &PairingProxy,
    state: Arc<Mutex<State>>,
    line: String,
) -> Result<ReplControl, Error> {
    match parse_cmd(line) {
        ParseResult::Valid((cmd, args)) => {
            handle_cmd(bt_svc, host_svc, pairing_svc, state, cmd, args).await
        }
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
    access_svc: &AccessProxy,
    host_svc: &HostWatcherProxy,
    pairing_svc: &PairingProxy,
    state: Arc<Mutex<State>>,
    cmd: Cmd,
    args: Vec<String>,
) -> Result<ReplControl, Error> {
    let args: Vec<&str> = args.iter().map(|s| s.as_str()).collect();
    let args: &[&str] = &*args;
    let res = match cmd {
        Cmd::Connect => connect(args, &state, &access_svc, None).await,
        Cmd::Bond => connect(args, &state, &access_svc, Some(&pairing_svc)).await,
        Cmd::Disconnect => disconnect(args, &state, &access_svc).await,
        Cmd::Pair => pair(args, &state, &access_svc).await,
        Cmd::AllowPairing => allow_pairing(args, &pairing_svc).await,
        Cmd::Forget => forget(args, &state, &access_svc).await,
        Cmd::StartDiscovery => set_discovery(true, &state, &access_svc).await,
        Cmd::StopDiscovery => set_discovery(false, &state, &access_svc).await,
        Cmd::Discoverable => set_discoverable(true, &access_svc, &state).await,
        Cmd::NotDiscoverable => set_discoverable(false, &access_svc, &state).await,
        Cmd::GetPeers => Ok(get_peers(args, &state, false)),
        Cmd::ShowPeers => Ok(get_peers(args, &state, true)),
        Cmd::GetPeer => Ok(get_peer(args, &state)),
        Cmd::GetAdapters => get_hosts(&state).await,
        Cmd::SetActiveAdapter => set_active_host(args, &host_svc).await,
        Cmd::SetAdapterName => set_local_name(args, &access_svc).await,
        Cmd::SetAdapterDeviceClass => set_device_class(args, &access_svc).await,
        Cmd::ActiveAdapter => get_active_host(&state).await,
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
) -> (impl Stream<Item = String>, impl Sink<(), Error = mpsc::SendError>) {
    // Editor thread and command processing thread must be synchronized so that output
    // is printed in the correct order.
    let (mut cmd_sender, cmd_receiver) = mpsc::channel(0);
    let (ack_sender, mut ack_receiver) = mpsc::channel(0);

    let _ = thread::spawn(move || -> Result<(), Error> {
        let mut exec =
            fasync::LocalExecutor::new().context("error creating readline event loop")?;

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
                if ack_receiver.next().await.is_none() {
                    return Ok(());
                };
            }
        };
        exec.run_singlethreaded(fut)
    });
    (cmd_receiver, ack_sender)
}

async fn watch_peers(access_svc: AccessProxy, state: Arc<Mutex<State>>) -> Result<(), Error> {
    // Used to avoid printing all peers on first watch_peers() call.
    let mut first_loop = true;
    loop {
        let (updated, removed) = access_svc.watch_peers().await?;
        for peer in updated.into_iter() {
            let peer = Peer::try_from(peer).context("Malformed FIDL peer")?;
            if !first_loop {
                print!("{}", CLEAR_LINE);
                print_peer_state_updates(&state.lock(), &peer);
            }
            let _ = state.lock().peers.insert(peer.id, peer);
        }
        for id in removed.into_iter() {
            let peer_id = PeerId::try_from(id).context("Malformed FIDL peer id")?;
            let _ = state.lock().peers.remove(&peer_id);
        }
        first_loop = false;
    }
}

async fn watch_hosts(host_svc: HostWatcherProxy, state: Arc<Mutex<State>>) -> Result<(), Error> {
    let mut first_result = true;
    loop {
        let fidl_hosts = host_svc.watch().await?;
        let mut hosts = Vec::<HostInfo>::new();
        if !first_result && !hosts.is_empty() {
            print!("{}", CLEAR_LINE);
        }
        for fidl_host in &fidl_hosts {
            let host = HostInfo::try_from(fidl_host)?;
            if !first_result {
                println!(
                    "Adapter updated: [address: {}, active: {}, discoverable: {}, discovering: {}]",
                    host.address, host.active, host.discoverable, host.discovering
                );
            }
            hosts.push(host);
        }
        state.lock().hosts = hosts;
        first_result = false;
    }
}

/// REPL execution
async fn run_repl(
    access_svc: AccessProxy,
    host_svc: HostWatcherProxy,
    pairing_svc: PairingProxy,
    state: Arc<Mutex<State>>,
) -> Result<(), Error> {
    // `cmd_stream` blocks on input in a separate thread and passes commands and acks back to
    // the main thread via async channels.
    let (mut commands, mut acks) = cmd_stream(state.clone());

    while let Some(cmd) = commands.next().await {
        match parse_and_handle_cmd(&access_svc, &host_svc, &pairing_svc, state.clone(), cmd).await {
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
    let access_svc = connect_to_protocol::<AccessMarker>()
        .context("failed to connect to bluetooth access interface")?;
    let host_watcher_svc =
        connect_to_protocol::<HostWatcherMarker>().context("failed to watch hosts")?;
    let pairing_svc = connect_to_protocol::<PairingMarker>()
        .context("failed to connect to bluetooth pairing interface")?;
    let state = Arc::new(Mutex::new(State::new()));
    let peer_watcher = watch_peers(access_svc.clone(), state.clone());
    let repl = run_repl(access_svc, host_watcher_svc.clone(), pairing_svc, state.clone())
        .map_err(|e| e.context("REPL failed unexpectedly").into());
    let host_watcher = watch_hosts(host_watcher_svc, state);
    pin_mut!(repl);
    pin_mut!(peer_watcher);
    select! {
        r = repl.fuse() => r,
        p = peer_watcher.fuse() => p,
        h = host_watcher.fuse() => h,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use {
        assert_matches::assert_matches,
        bt_fidl_mocks::sys::{AccessMock, PairingMock},
        fidl::endpoints::Proxy,
        fidl_fuchsia_bluetooth as fbt, fidl_fuchsia_bluetooth_sys as fsys,
        fidl_fuchsia_bluetooth_sys::{InputCapability, OutputCapability},
        fuchsia_bluetooth::types::{Address, PeerId},
        fuchsia_zircon::{Duration, DurationNum},
        futures::join,
        parking_lot::Mutex,
        std::task::Poll,
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

    fn custom_host(
        id: HostId,
        address: Address,
        active: bool,
        discoverable: bool,
        discovering: bool,
        name: Option<String>,
    ) -> HostInfo {
        HostInfo {
            id,
            technology: fsys::TechnologyType::LowEnergy,
            address,
            active,
            local_name: name,
            discoverable,
            discovering,
            addresses: vec![address],
        }
    }

    fn state_with(p: Peer) -> State {
        let mut state = State::new();
        let _ = state.peers.insert(p.id, p);
        state
    }

    #[fuchsia::test]
    async fn test_get_hosts() {
        // Fields for table view of hosts
        let fields = Regex::new(r"HostId[ \t]*\|[ \t]*Address[ \t]*\|[ \t]*Active[ \t]*\|[ \t]*Technology[ \t]*\|[ \t]*Name[ \t]*\|[ \t]*Discoverable[ \t]*\|[ \t]*Discovering").unwrap();

        // No hosts
        let mut output = get_hosts(&Mutex::new(State::new())).await.unwrap();
        assert!(!fields.is_match(&output));
        assert!(output.contains("No adapters"));

        let mut state = State::new();
        state.hosts.push(custom_host(
            HostId(0xbeef),
            Address::Public([0x11, 0x00, 0x55, 0x7E, 0xDE, 0xAD]),
            false,
            false,
            false,
            Some("Sapphire".to_string()),
        ));
        state.hosts.push(custom_host(
            HostId(0xabcd),
            Address::Random([0x22, 0x00, 0x55, 0x7E, 0xDE, 0xAD]),
            false,
            false,
            true,
            None,
        ));
        let state = Mutex::new(state);

        // Hosts exist
        output = get_hosts(&state).await.unwrap();
        assert!(fields.is_match(&output));
        assert!(output.contains("ef"));
        assert!(output.contains("cd"));
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
    fn test_get_peers_full_details() {
        let mut state = State::new();
        let _ = state.peers.insert(
            PeerId(0xabcd),
            named_peer(PeerId(0xabcd), Address::Public([0xAB, 0x89, 0x67, 0x45, 0x23, 0x01]), None),
        );
        let _ = state.peers.insert(
            PeerId(0xbeef),
            named_peer(
                PeerId(0xbeef),
                Address::Public([0x11, 0x00, 0x55, 0x7E, 0xDE, 0xAD]),
                Some("Sapphire".to_string()),
            ),
        );
        let state = Mutex::new(state);

        let get_peers =
            |args: &[&str], state: &Mutex<State>| -> String { get_peers(args, state, true) };

        // Fields for detailed view of peers
        let fields = Regex::new(r"Id(?s).*Address(?s).*Technology(?s).*Name(?s).*Appearance(?s).*Connected(?s).*Bonded(?s).*LE Services(?s).*BR/EDR Serv\.").unwrap();

        // Empty arguments matches everything
        assert!(fields.is_match(&get_peers(&[], &state)));
        assert!(get_peers(&[], &state).contains("2/2 peers"));
        assert!(get_peers(&[], &state).contains("01:23:45"));
        assert!(get_peers(&[], &state).contains("AD:DE:7E"));

        // No matches prints nothing.
        assert!(!fields.is_match(&get_peers(&["nomatch"], &state)));
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
    fn test_get_peers_less_details() {
        let mut state = State::new();
        let _ = state.peers.insert(
            PeerId(0xabcd),
            named_peer(PeerId(0xabcd), Address::Public([0xAB, 0x89, 0x67, 0x45, 0x23, 0x01]), None),
        );
        let _ = state.peers.insert(
            PeerId(0xbeef),
            named_peer(
                PeerId(0xbeef),
                Address::Public([0x11, 0x00, 0x55, 0x7E, 0xDE, 0xAD]),
                Some("Sapphire".to_string()),
            ),
        );
        let state = Mutex::new(state);

        let get_peers =
            |args: &[&str], state: &Mutex<State>| -> String { get_peers(args, state, false) };

        // Fields for table view of peers
        let fields = Regex::new(r"PeerId[ \t]*\|[ \t]*Address[ \t]*\|[ \t]*Technology[ \t]*\|[ \t]*Name[ \t]*\|[ \t]*Appearance[ \t]*\|[ \t]*Connected[ \t]*\|[ \t]*Bonded").unwrap();

        // Empty arguments matches everything
        assert!(fields.is_match(&get_peers(&[], &state)));
        assert!(get_peers(&[], &state).contains("2/2 peers"));
        assert!(get_peers(&[], &state).contains("01:23:45"));
        assert!(get_peers(&[], &state).contains("AD:DE:7E"));

        // No matches prints nothing.
        assert!(!fields.is_match(&get_peers(&["nomatch"], &state)));
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
                None => State::new(),
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
            ("disconnect deadbeef", Ok(PeerId(0xdeadbeef))),
            // unknown address
            (
                "disconnect 00:00:00:00:00:00",
                Err("Unable to disconnect: Unknown address 00:00:00:00:00:00".to_string()),
            ),
            // known address
            ("disconnect 00:00:00:00:00:01", Ok(PeerId(0xdeadbeef))),
            // no id param
            ("disconnect", Err(format!("usage: {}", Cmd::Disconnect.cmd_help()))),
        ];
        for (line, expected) in cases {
            assert_eq!(parse_disconnect_id(line, &state), expected);
        }
    }

    fn parse_disconnect_id(line: &str, state: &Mutex<State>) -> Result<PeerId, String> {
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
            ("T", Ok(BondableMode::Bondable)),
            ("f", Ok(BondableMode::NonBondable)),
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
                        bondable_mode: Some(BondableMode::Bondable),
                        transport: Some(TechnologyType::LowEnergy),
                        ..PairingOptions::EMPTY
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
                        bondable_mode: Some(BondableMode::NonBondable),
                        transport: None,
                        ..PairingOptions::EMPTY
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

    #[fuchsia::test(allow_stalls = false)]
    async fn test_disconnect() {
        let peer = peer(true, false);
        let peer_id = peer.id;
        let peer_id_string = peer.id.to_string();
        let args = vec![peer_id_string.as_str()];
        let state = Mutex::new(state_with(peer));
        let (proxy, mut mock) = AccessMock::new(timeout()).expect("failed to create mock");

        let cmd = disconnect(args.as_slice(), &state, &proxy);
        let mock_expect = mock.expect_disconnect(peer_id.into(), Ok(()));
        let (result, mock_result) = join!(cmd, mock_expect);

        let _ = mock_result.expect("mock FIDL expectation not satisfied");
        assert_eq!("".to_string(), result.expect("expected success"));
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_disconnect_error() {
        let peer = peer(true, false);
        let peer_id = peer.id;
        let peer_id_string = peer.id.to_string();
        let args = vec![peer_id_string.as_str()];
        let state = Mutex::new(state_with(peer));
        let (proxy, mut mock) = AccessMock::new(timeout()).expect("failed to create mock");

        let cmd = disconnect(args.as_slice(), &state, &proxy);
        let mock_expect = mock.expect_disconnect(peer_id.into(), Err(fsys::Error::Failed));
        let (result, mock_result) = join!(cmd, mock_expect);

        let _ = mock_result.expect("mock FIDL expectation not satisfied");
        assert!(result.expect("expected a result").contains("Disconnect error"));
    }

    #[test]
    fn test_allow_pairing_no_args() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let args = vec![];
        let (proxy, mut mock) = PairingMock::new(1.second()).expect("failed to create mock");
        let pair = allow_pairing(args.as_slice(), &proxy);
        pin_mut!(pair);

        assert!(exec.run_until_stalled(&mut pair).is_pending());

        let mock_expect =
            mock.expect_set_pairing_delegate(InputCapability::None, OutputCapability::None);

        assert!(exec.run_singlethreaded(mock_expect).is_ok());
    }

    #[fuchsia::test]
    fn test_allow_pairing_args() {
        let mut exec = fasync::TestExecutor::new().unwrap();

        let (proxy, mut mock) = PairingMock::new(1.second()).expect("failed to create mock");

        // Enable pairing with confirmation input cap and display output cap.
        let args = vec!["confirmation", "display"];
        let pair = allow_pairing(args.as_slice(), &proxy);
        pin_mut!(pair);

        // Should be waiting until pairing is completed.
        assert!(exec.run_until_stalled(&mut pair).is_pending());

        // Expect pairing delegate to be set with the correct capabilities.
        let proxy = exec
            .run_singlethreaded(mock.expect_set_pairing_delegate(
                InputCapability::Confirmation,
                OutputCapability::Display,
            ))
            .expect("cannot get proxy");
        assert!(!proxy.is_closed());

        // Force closing of the proxy.
        std::mem::drop(proxy);

        // Verify that allow_pairing existed with error.
        assert_matches!(exec.run_until_stalled(&mut pair), Poll::Ready(Err(_)));
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_allow_pairing_error() {
        // Arguments that don't correspond to any capabilities.
        let args = vec!["nonsense", "fake"];
        let (proxy, _mock) = PairingMock::new(1.second()).expect("failed to create mock");

        assert!(allow_pairing(args.as_slice(), &proxy).await.is_err());

        // Incorrect number of arguments.
        let args = vec!["none"];
        let (proxy, _mock) = PairingMock::new(1.second()).expect("failed to create mock");

        assert!(allow_pairing(args.as_slice(), &proxy).await.is_err());
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_forget() {
        let peer = peer(true, false);
        let peer_id = peer.id;
        let peer_id_string = peer.id.to_string();
        let args = vec![peer_id_string.as_str()];
        let state = Mutex::new(state_with(peer));
        let (proxy, mut mock) = AccessMock::new(1.second()).expect("failed to create mock");

        let cmd = forget(args.as_slice(), &state, &proxy);
        let mock_expect = mock.expect_forget(peer_id.into(), Ok(()));
        let (result, mock_result) = join!(cmd, mock_expect);

        assert!(mock_result.is_ok());
        assert_eq!("".to_string(), result.expect("expected success"));
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_forget_error() {
        let peer = peer(true, false);
        let peer_id = peer.id;
        let peer_id_string = peer.id.to_string();
        let args = vec![peer_id_string.as_str()];
        let state = Mutex::new(state_with(peer));
        let (proxy, mut mock) = AccessMock::new(1.second()).expect("failed to create mock");

        let cmd = forget(args.as_slice(), &state, &proxy);
        let mock_expect = mock.expect_forget(peer_id.into(), Err(fsys::Error::Failed));
        let (result, mock_result) = join!(cmd, mock_expect);

        assert!(mock_result.is_ok());
        assert!(result.expect("expected a result").contains("Forget error"));
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_pair() {
        let peer =
            custom_peer(PeerId(0xbeef), Address::Public([1, 0, 0, 0, 0, 0]), true, false, None);
        let peer_id = peer.id;
        let peer_id_string = peer.id.to_string();
        let pairing_options = PairingOptions {
            le_security_level: Some(PairingSecurityLevel::Encrypted),
            bondable_mode: Some(BondableMode::Bondable),
            transport: None,
            ..PairingOptions::EMPTY
        };

        let args = vec![peer_id_string.as_str(), "ENC", "T"];
        let state = Mutex::new(state_with(peer));
        let (proxy, mut mock) = AccessMock::new(1.second()).expect("failed to create mock");

        let cmd = pair(args.as_slice(), &state, &proxy);
        let mock_expect = mock.expect_pair(peer_id.into(), pairing_options, Ok(()));
        let (result, mock_result) = join!(cmd, mock_expect);

        assert!(mock_result.is_ok());
        assert_eq!("".to_string(), result.expect("expected success"));
    }

    #[fuchsia::test(allow_stalls = false)]
    async fn test_pair_error() {
        let peer =
            custom_peer(PeerId(0xbeef), Address::Public([1, 0, 0, 0, 0, 0]), true, false, None);
        let peer_id = peer.id;
        let peer_id_string = peer.id.to_string();
        let pairing_options = PairingOptions {
            le_security_level: Some(PairingSecurityLevel::Encrypted),
            bondable_mode: Some(BondableMode::Bondable),
            transport: None,
            ..PairingOptions::EMPTY
        };

        let args = vec![peer_id_string.as_str(), "ENC", "T"];
        let state = Mutex::new(state_with(peer));
        let (proxy, mut mock) = AccessMock::new(1.second()).expect("failed to create mock");

        let cmd = pair(args.as_slice(), &state, &proxy);
        let mock_expect =
            mock.expect_pair(peer_id.into(), pairing_options, Err(fsys::Error::Failed));
        let (result, mock_result) = join!(cmd, mock_expect);

        assert!(mock_result.is_ok());
        assert!(result.expect("expected a result").contains("Pair error"));
    }
}
