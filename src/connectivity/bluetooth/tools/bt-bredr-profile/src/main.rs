// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "256"]

use {
    anyhow::{anyhow, Context, Error},
    bt_rfcomm::ServerChannel,
    fidl::{encoding::Decodable, endpoints::create_request_stream},
    fidl_fuchsia_bluetooth_bredr::*,
    fidl_fuchsia_bluetooth_rfcomm_test::RfcommTestMarker,
    fuchsia_async::{self as fasync, futures::select},
    fuchsia_bluetooth::types::{PeerId, Uuid},
    fuchsia_component::client::connect_to_protocol,
    futures::{
        channel::{
            mpsc::{channel, SendError},
            oneshot,
        },
        lock::Mutex,
        FutureExt, Sink, SinkExt, Stream, StreamExt, TryStreamExt,
    },
    rustyline::{error::ReadlineError, CompletionType, Config, Editor},
    std::{convert::TryFrom, sync::Arc, thread},
    test_rfcomm_client::RfcommManager,
};

use crate::{
    commands::{Cmd, CmdHelper, ReplControl},
    types::*,
};

mod commands;
mod types;

/// Prompt to be shown for tool's REPL
pub static PROMPT: &str = "\x1b[34mprofile>\x1b[0m ";

/// Escape code to clear the pty line on which the cursor is located.
/// Used when evented output is intermingled with the REPL prompt.
pub static RESET_LINE: &str = "\x1b[2K\r";

async fn channels(state: Arc<Mutex<ProfileState>>) {
    for (chan_id, chan) in state.lock().await.l2cap_channels.map() {
        print!(
            "Channel:\n  Id: {}\n  Mode: {:?}\n  Max Tx Sdu Size: {}\n",
            chan_id, chan.mode, chan.max_tx_sdu_size
        );
    }
}

fn channel_mode_from_str(s: &str) -> Result<ChannelMode, Error> {
    match s {
        "basic" | "b" => Ok(ChannelMode::Basic),
        "ertm" | "e" => Ok(ChannelMode::EnhancedRetransmission),
        s => Err(anyhow!("Invalid channel mode {}", s)),
    }
}

fn security_requirements_from_str(s: &str) -> Result<Option<SecurityRequirements>, Error> {
    match s {
        "none" => Ok(None),
        "auth" => Ok(Some(SecurityRequirements {
            authentication_required: Some(true),
            secure_connections_required: None,
            ..SecurityRequirements::EMPTY
        })),
        "sc" => Ok(Some(SecurityRequirements {
            authentication_required: None,
            secure_connections_required: Some(true),
            ..SecurityRequirements::EMPTY
        })),
        "auth-sc" => Ok(Some(SecurityRequirements {
            authentication_required: Some(true),
            secure_connections_required: Some(true),
            ..SecurityRequirements::EMPTY
        })),
        s => Err(anyhow!("Invalid security requirements {}", s)),
    }
}

/// Listen on the control event channel for new events.
async fn connection_receiver(
    mut connection_requests: ConnectionReceiverRequestStream,
    end_ad_receiver: oneshot::Receiver<()>,
    state: Arc<Mutex<ProfileState>>,
    service_id: u32,
) -> Result<(), Error> {
    let tag = "ConnectionReceiver";
    let mut end_ad_receiver = end_ad_receiver.fuse();
    loop {
        select! {
          request = connection_requests.try_next() => {
            let event = match request {
                Err(e) => return Err(anyhow!("{} error: {:?}", tag, e)),
                Ok(None) => return Err(anyhow!("{} channel closed", tag)),
                Ok(Some(event)) => event,
            };
            let ConnectionReceiverRequest::Connected { channel, .. } = event;
            let socket = channel.socket.ok_or(anyhow!("{}: missing socket", tag))?;
            let mode = channel.channel_mode.ok_or(anyhow!("{}: missing channel mode", tag))?;
            let max_tx_sdu_size = channel.max_tx_sdu_size.ok_or(anyhow!("{}: missing max tx sdu", tag))?;
            let chan_id = state.lock().await.l2cap_channels.insert(L2capChannel { socket, mode, max_tx_sdu_size });
            print!("{} Channel Connected to service {}:\n  Channel:\n    Id: {}\n    Mode: {:?}\n    Max Tx Sdu Size: {}\n", RESET_LINE, service_id, chan_id, mode, max_tx_sdu_size);

        },
            _ = end_ad_receiver => break,
            complete => break,
        }
    }
    print!("{} ConnectionReceiver closed for service {}", RESET_LINE, service_id);
    let args = vec![format!("{}", service_id)];
    remove_service(state, &args).await
}

async fn setup_rfcomm(state: Arc<Mutex<ProfileState>>) -> Result<(), Error> {
    state.lock().await.rfcomm.advertise()
}

async fn advertise(
    profile_svc: &ProfileProxy,
    state: Arc<Mutex<ProfileState>>,
    args: &Vec<String>,
) -> Result<(), Error> {
    if args.len() != 3 {
        println!("Error: invalid number of arguments");
        println!("{}", Cmd::help_msg());
        return Ok(());
    }

    let psm = args[0].parse::<u16>().map_err(|_| anyhow!("psm must be a 16-bit integer"))?;
    let channel_mode =
        channel_mode_from_str(args[1].as_ref()).map_err(|_| anyhow!("invalid channel mode"))?;
    let max_rx_sdu_size = args[2]
        .parse::<u16>()
        .map_err(|_| anyhow!("max-rx-sdu-size must be an integer i the range 0 - 65535"))?;
    let params = ChannelParameters {
        channel_mode: Some(channel_mode),
        max_rx_sdu_size: Some(max_rx_sdu_size),
        security_requirements: None,
        ..ChannelParameters::EMPTY
    };

    let audio_sink_uuid = Uuid::new16(0x110B); // Audio Sink

    let svc_defs = vec![ServiceDefinition {
        service_class_uuids: Some(vec![audio_sink_uuid.into()]),
        protocol_descriptor_list: Some(vec![ProtocolDescriptor {
            protocol: ProtocolIdentifier::L2Cap,
            params: vec![DataElement::Uint16(psm)],
        }]),
        ..ServiceDefinition::new_empty()
    }];

    let (connect_client, connect_requests) =
        create_request_stream().context("ConnectionReceiver creation")?;

    let _ = profile_svc.advertise(&mut svc_defs.into_iter(), params, connect_client).check()?;

    let (end_ad_sender, end_ad_receiver) = oneshot::channel::<()>();
    let service_id = state.lock().await.services.insert(SdpService {
        advertisement_stopper: end_ad_sender,
        params: ChannelParameters {
            channel_mode: Some(channel_mode),
            max_rx_sdu_size: Some(max_rx_sdu_size),
            security_requirements: None,
            ..ChannelParameters::EMPTY
        },
    });

    print!("Advertised Service Id: {}\n", service_id);

    let request_handler_fut =
        connection_receiver(connect_requests, end_ad_receiver, state.clone(), service_id);
    fasync::Task::spawn(async move {
        if let Err(e) = request_handler_fut.await {
            print!("{} ConnectionReceiver ended with error: {:?}", RESET_LINE, e);
        }
    })
    .detach();

    Ok(())
}

async fn remove_service(state: Arc<Mutex<ProfileState>>, args: &Vec<String>) -> Result<(), Error> {
    if args.len() != 1 {
        return Err(anyhow!("Invalid number of arguments"));
    }

    let service_id =
        args[0].parse::<u32>().map_err(|_| anyhow!("service-id must be a positive number"))?;

    state.lock().await.services.remove(&service_id).ok_or(anyhow!("Unknown service"))?;
    Ok(())
}

async fn services(state: Arc<Mutex<ProfileState>>) {
    for (id, service) in &state.lock().await.services {
        print!(
            "Service:\n  Id: {}\n  Mode: {:?}, Max Rx Sdu Size: {}",
            id,
            service.params.channel_mode.unwrap(),
            service.params.max_rx_sdu_size.unwrap()
        );
    }
}

async fn connect_l2cap(
    profile_svc: &ProfileProxy,
    state: Arc<Mutex<ProfileState>>,
    args: &Vec<String>,
) -> Result<(), Error> {
    if args.len() != 5 {
        return Err(anyhow!("Invalid number of arguments"));
    }
    let peer_id: PeerId = args[0].parse()?;
    let psm = args[1].parse::<u16>().map_err(|_| anyhow!("Psm must be [0, 65535]"))?;
    let channel_mode = channel_mode_from_str(args[2].as_ref())?;
    let max_rx_sdu_size =
        args[3].parse::<u16>().map_err(|_| anyhow!("max-sdu-size must be [0, 65535]"))?;
    let security_requirements = security_requirements_from_str(args[4].as_ref())?;
    let params = ChannelParameters {
        channel_mode: Some(channel_mode),
        max_rx_sdu_size: Some(max_rx_sdu_size),
        security_requirements,
        ..ChannelParameters::EMPTY
    };

    let channel = match profile_svc
        .connect(
            &mut peer_id.into(),
            &mut ConnectParameters::L2cap(L2capParameters {
                psm: Some(psm),
                parameters: Some(params),
                ..L2capParameters::new_empty()
            }),
        )
        .await?
    {
        Err(e) => return Err(anyhow!("Could not connect to {}: {:?}", peer_id, e)),
        Ok(channel) => channel,
    };

    let mode = match channel.channel_mode {
        Some(m) => m,
        None => return Err(anyhow!("Missing channel mode in response")),
    };

    let max_tx_sdu_size = match channel.max_tx_sdu_size {
        Some(s) => s,
        None => return Err(anyhow!("Missing max tx sdu size in response")),
    };

    let chan_id = match channel.socket {
        Some(socket) => {
            state.lock().await.l2cap_channels.insert(L2capChannel { socket, mode, max_tx_sdu_size })
        }
        None => {
            println!("Error: failed to receive a socket");
            return Ok(());
        }
    };

    print!(
        "Channel:\n  Id: {}\n  Mode: {:?}\n  Max Tx Sdu Size: {}\n",
        chan_id, mode, max_tx_sdu_size
    );

    Ok(())
}

/// Attempts to make an outbound RFCOMM connection to the peer specified in `args`.
async fn connect_rfcomm(state: Arc<Mutex<ProfileState>>, args: &Vec<String>) -> Result<(), Error> {
    if args.len() != 2 {
        return Err(anyhow!("Invalid number of arguments"));
    }
    let peer_id: PeerId = args[0].parse()?;
    let server_channel =
        args[1].parse::<u8>().map_err(|_| anyhow!("Server channel must be a u8"))?;
    let server_channel = ServerChannel::try_from(server_channel)?;

    state.lock().await.rfcomm.outgoing_rfcomm_channel(peer_id, server_channel).await
}

async fn disconnect_l2cap(
    state: Arc<Mutex<ProfileState>>,
    args: &Vec<String>,
) -> Result<(), Error> {
    if args.len() != 1 {
        return Err(anyhow!("Invalid number of arguments"));
    }

    let chan_id = args[0].parse::<u32>().map_err(|_| anyhow!("channel-id must be an integer"))?;

    match state.lock().await.l2cap_channels.remove(&chan_id) {
        Some(_) => println!("Channel {} disconnected", chan_id),
        None => println!("No channel with id {} exists", chan_id),
    }
    Ok(())
}

async fn disconnect_rfcomm(
    state: Arc<Mutex<ProfileState>>,
    args: &Vec<String>,
) -> Result<(), Error> {
    if args.len() != 2 {
        return Err(anyhow!("Invalid number of arguments"));
    }

    let peer_id: PeerId = args[0].parse()?;
    let server_channel =
        args[1].parse::<u8>().map_err(|_| anyhow!("Server channel must be a u8"))?;
    let server_channel = ServerChannel::try_from(server_channel)?;

    match state.lock().await.rfcomm.close_rfcomm_channel(peer_id, server_channel) {
        Ok(()) => println!("RFCOMM Channel {:?} disconnected", server_channel),
        Err(e) => println!("Couldn't close RFCOMM channel: {:?}", e),
    }
    Ok(())
}

async fn disconnect_rfcomm_session(
    state: Arc<Mutex<ProfileState>>,
    args: &Vec<String>,
) -> Result<(), Error> {
    if args.len() != 1 {
        return Err(anyhow!("Invalid number of arguments"));
    }

    let peer_id: PeerId = args[0].parse()?;
    match state.lock().await.rfcomm.close_session(peer_id) {
        Ok(_) => println!("Closed RFCOMM Session with peer {:?}", peer_id),
        Err(e) => println!("Error closing RFCOMM Session with peer {:?}: {:?}", peer_id, e),
    }
    Ok(())
}

async fn send_rls(state: Arc<Mutex<ProfileState>>, args: &Vec<String>) -> Result<(), Error> {
    if args.len() != 2 {
        return Err(anyhow!("Invalid number of arguments"));
    }

    let peer_id: PeerId = args[0].parse()?;
    let server_channel =
        args[1].parse::<u8>().map_err(|_| anyhow!("Server channel must be a u8"))?;
    let server_channel = ServerChannel::try_from(server_channel)?;

    match state.lock().await.rfcomm.send_rls(peer_id, server_channel) {
        Ok(_) => println!("Sent RLS to peer {:?}", peer_id),
        Err(e) => println!("Error sending RLS to peer {:?}: {:?}", peer_id, e),
    }
    Ok(())
}

async fn write_l2cap(state: Arc<Mutex<ProfileState>>, args: &Vec<String>) -> Result<(), Error> {
    if args.len() != 2 {
        return Err(anyhow!("Invalid number of arguments"));
    }

    let chan_id = args[0].parse::<u32>().map_err(|_| anyhow!("channel-id must be an integer"))?;
    let bytes = args[1].as_bytes();

    let num_bytes = match state.lock().await.l2cap_channels.map().get(&chan_id) {
        Some(chan) => chan.socket.write(bytes).map_err(|_| anyhow!("error writing data"))?,
        None => return Err(anyhow!("No channel with id {} exists", chan_id)),
    };
    println!("{} bytes written", num_bytes);

    Ok(())
}

/// Sends a user data payload to the ServerChannel specified in `args`.
async fn write_rfcomm(state: Arc<Mutex<ProfileState>>, args: &Vec<String>) -> Result<(), Error> {
    if args.len() != 3 {
        return Err(anyhow!("Invalid number of arguments"));
    }

    let peer_id: PeerId = args[0].parse()?;
    let server_channel =
        args[1].parse::<u8>().map_err(|_| anyhow!("Server channel must be a u8"))?;
    let server_channel = ServerChannel::try_from(server_channel)?;
    let bytes = args[2].as_bytes().to_vec();
    state.lock().await.rfcomm.send_user_data(peer_id, server_channel, bytes)
}

async fn cleanup(state: Arc<Mutex<ProfileState>>) {
    state.lock().await.reset();
}

enum ParsedCmd {
    Valid(Cmd, Vec<String>),
    Empty,
}

/// Parse a single raw input command from a user into (command type, argument list)
fn parse_cmd(line: String) -> Result<ParsedCmd, Error> {
    let components: Vec<_> = line.trim().split_whitespace().collect();
    match components.split_first() {
        Some((raw_cmd, args)) => match raw_cmd.parse() {
            Ok(cmd) => {
                let args = args.into_iter().map(|s| s.to_string()).collect();
                Ok(ParsedCmd::Valid(cmd, args))
            }
            Err(_) => Err(anyhow!("\"{}\" is not a valid command", raw_cmd)),
        },
        None => Ok(ParsedCmd::Empty),
    }
}

async fn handle_cmd(
    profile_svc: &ProfileProxy,
    state: Arc<Mutex<ProfileState>>,
    cmd: Cmd,
    args: Vec<String>,
) -> Result<ReplControl, Error> {
    match cmd {
        Cmd::Advertise => advertise(profile_svc, state.clone(), &args).await?,
        Cmd::RemoveService => remove_service(state.clone(), &args).await?,
        Cmd::Services => services(state.clone()).await,
        Cmd::Channels => channels(state.clone()).await,
        Cmd::ConnectL2cap => connect_l2cap(profile_svc, state.clone(), &args).await?,
        Cmd::ConnectRfcomm => connect_rfcomm(state.clone(), &args).await?,
        Cmd::DisconnectL2cap => disconnect_l2cap(state.clone(), &args).await?,
        Cmd::DisconnectRfcomm => disconnect_rfcomm(state.clone(), &args).await?,
        Cmd::DisconnectRfcommSession => disconnect_rfcomm_session(state.clone(), &args).await?,
        Cmd::SetupRfcomm => setup_rfcomm(state.clone()).await?,
        Cmd::SendRls => send_rls(state.clone(), &args).await?,
        Cmd::WriteL2cap => write_l2cap(state.clone(), &args).await?,
        Cmd::WriteRfcomm => write_rfcomm(state.clone(), &args).await?,
        Cmd::Help => println!("{}", Cmd::help_msg()),
        Cmd::Exit | Cmd::Quit => {
            cleanup(state.clone()).await;
            return Ok(ReplControl::Break);
        }
    };
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
    let (mut cmd_sender, cmd_receiver) = channel(512);
    let (ack_sender, mut ack_receiver) = channel(512);

    thread::spawn(move || -> Result<(), Error> {
        let mut exec =
            fasync::LocalExecutor::new().context("error creating readline event loop")?;

        let fut = async {
            let config = Config::builder()
                .auto_add_history(true)
                .history_ignore_space(true)
                .completion_type(CompletionType::List)
                .build();
            let mut rl = Editor::<CmdHelper>::with_config(config);
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

                // Wait for processing thread to finish evaluating last command.
                if ack_receiver.next().await == None {
                    return Ok(());
                }
            }
        };

        exec.run_singlethreaded(fut)
    });
    (cmd_receiver, ack_sender)
}

/// Wait for raw commands from rustyline thread, and then parse and handle them.
async fn run_repl(profile_svc: ProfileProxy, state: Arc<Mutex<ProfileState>>) -> Result<(), Error> {
    // `cmd_stream` blocks on input in a separate thread and passes commands and acks back to
    // the main thread via async channels.
    let (mut commands, mut acks) = cmd_stream();

    while let Some(raw_cmd) = commands.next().await {
        match parse_cmd(raw_cmd) {
            Ok(ParsedCmd::Valid(cmd, args)) => {
                match handle_cmd(&profile_svc, state.clone(), cmd, args).await {
                    Ok(ReplControl::Continue) => {}
                    Ok(ReplControl::Break) => break,
                    Err(e) => println!("Error handling command: {}", e),
                }
            }
            Ok(ParsedCmd::Empty) => {}
            Err(err) => println!("Error parsing command: {}", err),
        }
        // Notify readline loop that command has been evaluated.
        acks.send(()).await?
    }

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["bt-bredr-profile"]).expect("Can't init logger");

    let profile_svc = connect_to_protocol::<ProfileMarker>()
        .context("failed to connect to bluetooth profile service")?;
    let rfcomm_test_svc = connect_to_protocol::<RfcommTestMarker>()
        .context("failed to connect to RFCOMM Test protocol")?;

    let rfcomm_mgr = RfcommManager::from_proxy(profile_svc.clone(), rfcomm_test_svc);
    let state = Arc::new(Mutex::new(ProfileState::new(rfcomm_mgr)));

    run_repl(profile_svc, state.clone()).await
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fuchsia::test]
    async fn disconnect_l2cap_channel_succeeds() {
        let (profile, _profile_server) =
            fidl::endpoints::create_proxy_and_stream::<ProfileMarker>().unwrap();
        let (rfcomm_test, _rfcomm_test_server) =
            fidl::endpoints::create_proxy_and_stream::<RfcommTestMarker>().unwrap();
        let state = Arc::new(Mutex::new(ProfileState::new(RfcommManager::from_proxy(
            profile,
            rfcomm_test,
        ))));

        let (s, _) = fidl::Socket::create(fidl::SocketOpts::STREAM).unwrap();
        assert_eq!(
            0,
            state.lock().await.l2cap_channels.insert(L2capChannel {
                socket: s,
                mode: ChannelMode::Basic,
                max_tx_sdu_size: 672
            })
        );
        assert_eq!(1, state.lock().await.l2cap_channels.map().len());
        let args = vec!["0".to_string()];
        assert!(disconnect_l2cap(state.clone(), &args).await.is_ok());
        assert!(state.lock().await.l2cap_channels.map().is_empty());

        // Disconnecting an already disconnected channel should not fail.
        // (It should only print a message)
        assert!(disconnect_l2cap(state.clone(), &args).await.is_ok());
    }
}
