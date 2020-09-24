// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    argh::FromArgs,
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_bluetooth_avdtp::*,
    fuchsia_async as fasync,
    fuchsia_component::{client, fuchsia_single_component_package_url},
    futures::{
        channel::mpsc::{channel, SendError},
        try_join, FutureExt, Sink, SinkExt, Stream, StreamExt, TryStreamExt,
    },
    log::info,
    parking_lot::RwLock,
    pin_utils::pin_mut,
    rustyline::{error::ReadlineError, CompletionType, Config, EditMode, Editor},
    std::{collections::hash_map::Entry, collections::HashMap, sync::Arc, thread},
};

use crate::commands::{Cmd, CmdHelper, ReplControl};
use crate::types::{PeerFactoryMap, CLEAR_LINE, PROMPT};

mod commands;
mod types;

/// Command line arguments that the tool accepts.
/// Currently, the only supported argument at launch is the profile version.
#[derive(FromArgs)]
#[argh(description = "A2DP Profile")]
struct Options {
    /// a2dp profile to use with the tool (values: "sink", "source")
    #[argh(option, short = 'p')]
    profile: String,
}

/// Generates the next local identifier for the current session.
/// Increments linearly, and returns the id as a string.
// TODO(37089): Generate IDs based on the peer_id, and clean up state accordingly.
fn next_string(input: &str) -> String {
    (input.parse::<u64>().unwrap() + 1).to_string()
}

// TODO(37089): Spawn listener for PeerEventStream to delete peer from map
// when a peer disconnects from service.
async fn peer_manager_listener(
    avdtp_svc: &PeerManagerProxy,
    mut stream: PeerManagerEventStream,
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<(), Error> {
    let mut id_count = "0".to_string();
    while let Some(evt) = stream.try_next().await? {
        print!("{}", CLEAR_LINE);
        match evt {
            PeerManagerEvent::OnPeerConnected { mut peer_id } => {
                let id: String = format!("{:016x}", peer_id.value);

                let (client, server) = create_endpoints::<PeerControllerMarker>()
                    .expect("Failed to create peer endpoint");
                let peer = client.into_proxy().expect("Error: Couldn't obtain peer client proxy");

                match peer_map.write().entry(id_count.clone()) {
                    Entry::Occupied(_) => {}
                    Entry::Vacant(entry) => {
                        entry.insert(peer);
                        info!("Inserted device into PeerFactoryMap");
                    }
                };
                // Establish channel with the given peer_id and server endpoint.
                let _ = avdtp_svc.get_peer(&mut peer_id, server);
                info!("Getting peer with peer_id: {} and local id: {}", id, id_count);
                // Increment id count every time a new peer connects.
                id_count = next_string(&id_count);
            }
        }
    }
    Ok(())
}

async fn set_configuration<'a>(
    args: &'a [&'a str],
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<String, Error> {
    match get_controller_from_args(args, Cmd::SetConfig, peer_map) {
        Ok(peer_controller) => {
            let _ = peer_controller.set_configuration().await;
            Ok(String::from("Set configuration command"))
        }
        Err(e) => Err(e),
    }
}

async fn get_configuration<'a>(
    args: &'a [&'a str],
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<String, Error> {
    match get_controller_from_args(args, Cmd::GetConfig, peer_map) {
        Ok(peer_controller) => {
            let _ = peer_controller.get_configuration().await;
            Ok(String::from("Get configuration command"))
        }
        Err(e) => Err(e),
    }
}

async fn get_capabilities<'a>(
    args: &'a [&'a str],
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<String, Error> {
    match get_controller_from_args(args, Cmd::GetCapabilities, peer_map) {
        Ok(peer_controller) => {
            let _ = peer_controller.get_capabilities().await;
            Ok(String::from("Get capabilities command"))
        }
        Err(e) => Err(e),
    }
}

async fn get_all_capabilities<'a>(
    args: &'a [&'a str],
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<String, Error> {
    match get_controller_from_args(args, Cmd::GetAllCapabilities, peer_map) {
        Ok(peer_controller) => {
            let _ = peer_controller.get_all_capabilities().await;
            Ok(String::from("Get all capabilities command"))
        }
        Err(e) => Err(e),
    }
}

async fn reconfigure<'a>(
    args: &'a [&'a str],
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<String, Error> {
    match get_controller_from_args(args, Cmd::Reconfigure, peer_map) {
        Ok(peer_controller) => {
            let _ = peer_controller.reconfigure_stream().await;
            Ok(String::from("Reconfigure command"))
        }
        Err(e) => Err(e),
    }
}

async fn suspend<'a>(
    args: &'a [&'a str],
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<String, Error> {
    match get_controller_from_args(args, Cmd::Suspend, peer_map) {
        Ok(peer_controller) => {
            let _ = peer_controller.suspend_stream().await;
            Ok(String::from("Suspend command"))
        }
        Err(e) => Err(e),
    }
}

async fn suspend_reconfigure<'a>(
    args: &'a [&'a str],
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<String, Error> {
    match get_controller_from_args(args, Cmd::SuspendReconfigure, peer_map) {
        Ok(peer_controller) => {
            let _ = peer_controller.suspend_and_reconfigure().await;
            Ok(String::from("Suspend and reconfigure command"))
        }
        Err(e) => Err(e),
    }
}

async fn release_stream<'a>(
    args: &'a [&'a str],
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<String, Error> {
    match get_controller_from_args(args, Cmd::ReleaseStream, peer_map) {
        Ok(peer_controller) => {
            let _ = peer_controller.release_stream().await;
            Ok(String::from("Release stream command"))
        }
        Err(e) => Err(e),
    }
}

async fn establish_stream<'a>(
    args: &'a [&'a str],
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<String, Error> {
    match get_controller_from_args(args, Cmd::EstablishStream, peer_map) {
        Ok(peer_controller) => {
            let _ = peer_controller.establish_stream().await;
            Ok(String::from("Establish stream command"))
        }
        Err(e) => Err(e),
    }
}

async fn abort_stream<'a>(
    args: &'a [&'a str],
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<String, Error> {
    match get_controller_from_args(args, Cmd::AbortStream, peer_map) {
        Ok(peer_controller) => {
            let _ = peer_controller.abort_stream().await;
            Ok(String::from("Abort stream command"))
        }
        Err(e) => Err(e),
    }
}

async fn start_stream<'a>(
    args: &'a [&'a str],
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<String, Error> {
    match get_controller_from_args(args, Cmd::StartStream, peer_map) {
        Ok(peer_controller) => {
            let _ = peer_controller.start_stream().await;
            Ok(String::from("Start stream command"))
        }
        Err(e) => Err(e),
    }
}

fn get_controller_from_args<'a>(
    args: &'a [&'a str],
    cmd: Cmd,
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<PeerControllerProxy, Error> {
    if args.len() < 1 {
        return Err(format_err!("usage: {}", cmd.cmd_help()));
    }

    // Handle the case where user wants to understand command usage.
    if args[0] == "help" {
        return Err(format_err!("{}", cmd.cmd_help()));
    }

    let id = args[0];
    match peer_map.write().entry(id.to_string()) {
        Entry::Occupied(entry) => Ok(entry.get().clone()),
        Entry::Vacant(_) => {
            Err(format_err!("No peer with id: {:?}.\n Usage: {}", id, cmd.cmd_help()))
        }
    }
}

fn handle_help<'a>(args: &'a [&'a str]) -> Result<String, Error> {
    if args.len() == 0 {
        Ok(Cmd::help_msg().to_string())
    } else {
        match args[0].parse::<Cmd>() {
            Err(_) => Err(format_err!("Unsupported command: {}", args[0])),
            Ok(cmd) => Ok(cmd.cmd_help().to_string()),
        }
    }
}

/// Handle a single raw input command from a user and indicate whether the command should
/// result in continuation or breaking of the read evaluate print loop.
async fn handle_cmd<'a>(
    peer_map: Arc<RwLock<PeerFactoryMap>>,
    line: String,
) -> Result<ReplControl, Error> {
    let components: Vec<_> = line.trim().split_whitespace().collect();
    if components.is_empty() {
        return Ok(ReplControl::Continue);
    }
    let (raw_cmd, args) = components.split_first().expect("not empty");
    let cmd = raw_cmd.parse();

    let res = match cmd {
        Ok(Cmd::AbortStream) => abort_stream(args, peer_map).await,
        Ok(Cmd::EstablishStream) => establish_stream(args, peer_map).await,
        Ok(Cmd::ReleaseStream) => release_stream(args, peer_map).await,
        Ok(Cmd::StartStream) => start_stream(args, peer_map).await,
        Ok(Cmd::SetConfig) => set_configuration(args, peer_map).await,
        Ok(Cmd::GetConfig) => get_configuration(args, peer_map).await,
        Ok(Cmd::GetCapabilities) => get_capabilities(args, peer_map).await,
        Ok(Cmd::GetAllCapabilities) => get_all_capabilities(args, peer_map).await,
        Ok(Cmd::Reconfigure) => reconfigure(args, peer_map).await,
        Ok(Cmd::Suspend) => suspend(args, peer_map).await,
        Ok(Cmd::SuspendReconfigure) => suspend_reconfigure(args, peer_map).await,
        Ok(Cmd::Help) => handle_help(args),
        Ok(Cmd::Exit) | Ok(Cmd::Quit) => return Ok(ReplControl::Break),
        Err(_) => {
            info!("{} is not a valid command.", raw_cmd);
            Ok(format!("\"{}\" is not a valid command", raw_cmd))
        }
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
fn cmd_stream() -> (impl Stream<Item = String>, impl Sink<(), Error = SendError>) {
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
            let mut rl: Editor<CmdHelper> = Editor::with_config(config);
            rl.set_helper(Some(CmdHelper::new()));
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
async fn run_repl<'a>(peer_map: Arc<RwLock<PeerFactoryMap>>) -> Result<(), Error> {
    // `cmd_stream` blocks on input in a separate thread and passes commands and acks back to
    // the main thread via async channels.
    let (mut commands, mut acks) = cmd_stream();
    while let Some(cmd) = commands.next().await {
        match handle_cmd(peer_map.clone(), cmd).await {
            Ok(ReplControl::Continue) => {}
            Ok(ReplControl::Break) => {
                println!("\n");
                break;
            }
            Err(e) => {
                println!("Error handling command: {}", e);
            }
        }
        acks.send(()).await?;
    }
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: Options = argh::from_env();

    fuchsia_syslog::init_with_tags(&["bt-avdtp-tool"]).expect("Can't init logger");

    // Launch the A2DP `profile` locally, and connect to the local service.
    // If an invalid `profile` is given, the tool will exit with an error.
    let launcher = client::launcher().expect("Failed to launch bt-avdtp-tool service");
    let profile = if args.profile == "source".to_string() {
        fuchsia_single_component_package_url!("bt-a2dp-source").to_string()
    } else if args.profile == "sink".to_string() {
        fuchsia_single_component_package_url!("bt-a2dp-sink").to_string()
    } else {
        return Err(format_err!("Invalid A2DP profile. Exiting tool.").into());
    };

    let bt_a2dp = client::launch(&launcher, profile, None)?;
    info!("Running bt-avdtp-tool with A2DP {}", &args.profile);

    let avdtp_svc = bt_a2dp
        .connect_to_service::<PeerManagerMarker>()
        .context("Failed to connect to AVDTP Peer Manager")?;
    // Get the list of currently connected peers.
    // Since we are spinning up bt-a2dp-sink locally, there should be no connected peers.
    let _peers = avdtp_svc.connected_peers().await?;

    let evt_stream = avdtp_svc.take_event_stream();
    let peer_map: Arc<RwLock<PeerFactoryMap>> = Arc::new(RwLock::new(HashMap::new()));

    let event_fut = peer_manager_listener(&avdtp_svc, evt_stream, peer_map.clone()).fuse();
    let repl_fut = run_repl(peer_map.clone()).fuse();

    pin_mut!(event_fut);
    pin_mut!(repl_fut);

    try_join!(event_fut, repl_fut).map(|((), ())| ())
}

#[cfg(test)]
mod tests {
    use super::*;

    use fidl::endpoints::create_proxy;

    #[test]
    // Please note finishing bug 37089 will make next_string() obsolete.
    // This bug fix will introduce a better ID management system that is more robust.
    // This is a short term solution to pass PTS certifications.
    fn test_next_string() {
        let id = "0";
        let res = next_string(id);
        assert_eq!("1".to_string(), res);

        let id = "10";
        let res = next_string(id);
        assert_eq!("11".to_string(), res);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    /// Tests parsing input arguments works successfully.
    /// Success case: The id provided exists, and the PeerControllerProxy is correctly returned.
    /// Error case: Invalid id, Error string + help message returned.
    async fn test_get_controller_from_args() {
        let mut peer_map = HashMap::new();
        let (client_proxy, _server) =
            create_proxy::<PeerControllerMarker>().expect("Failed to create peer endpoint");

        let supported_id = "123";
        peer_map.insert(supported_id.to_string(), client_proxy.clone());
        let peer_map_obj = Arc::new(RwLock::new(peer_map));

        let invalid_id_args = ["2"];
        let res =
            get_controller_from_args(&invalid_id_args, Cmd::StartStream, peer_map_obj.clone());
        assert!(res.is_err());

        let valid_id_args = [supported_id];
        let res = get_controller_from_args(&valid_id_args, Cmd::StartStream, peer_map_obj);
        assert!(res.is_ok());
    }

    #[test]
    /// Tests parsing the help command works as intended.
    fn test_handle_help() {
        let invalid_cmd = ["foobar"];
        let res = handle_help(&invalid_cmd);
        assert_eq!(
            res.map_err(|e| format!("{:?}", e)),
            Err("Unsupported command: foobar".to_string())
        );

        let generic_help = [];
        let res = handle_help(&generic_help);
        assert!(res.is_ok());

        let cmd_help = ["set-configuration"];
        let res = handle_help(&cmd_help);
        assert!(res.is_ok());
    }
}
