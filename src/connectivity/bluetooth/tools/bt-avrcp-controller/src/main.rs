// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context as _, Error},
    fidl::encoding::Decodable as FidlDecodable,
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_bluetooth_avrcp::{
        self as fidl_avrcp, ControllerEvent, ControllerEventStream, ControllerMarker,
        ControllerProxy, Notifications, PeerManagerMarker, PlayerApplicationSettingAttributeId,
        MAX_ATTRIBUTES,
    },
    fidl_fuchsia_bluetooth_avrcp_test::{
        ControllerExtMarker, ControllerExtProxy, PeerManagerExtMarker,
    },
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    futures::{
        channel::mpsc::{channel, SendError},
        select, FutureExt, Sink, SinkExt, Stream, StreamExt, TryStreamExt,
    },
    hex::FromHex,
    pin_utils::pin_mut,
    rustyline::{error::ReadlineError, CompletionType, Config, EditMode, Editor},
    std::thread,
    structopt::StructOpt,
};

use crate::commands::{avc_match_string, Cmd, CmdHelper, ReplControl};

mod commands;

static PROMPT: &str = "\x1b[34mavrcp>\x1b[0m ";
/// Escape code to clear the pty line on which the cursor is located.
/// Used when evented output is intermingled with the REPL prompt.
static CLEAR_LINE: &str = "\x1b[2K";

/// Define the command line arguments that the tool accepts.
#[derive(StructOpt)]
#[structopt(
    version = "0.2.0",
    author = "Fuchsia Bluetooth Team",
    about = "Bluetooth AVRCP Controller CLI"
)]
struct Opt {
    #[structopt(help = "Target Device ID")]
    device: String,
}

async fn send_passthrough<'a>(
    args: &'a [&'a str],
    controller: &'a ControllerProxy,
) -> Result<String, Error> {
    if args.len() != 1 {
        return Ok(format!("usage: {}", Cmd::AvcCommand.cmd_help()));
    }
    let cmd = avc_match_string(args[0]);
    if cmd.is_none() {
        return Ok(String::from("invalid avc command"));
    }

    // `args[0]` is the identifier of the peer to connect to
    match controller.send_command(cmd.unwrap()).await? {
        Ok(_) => Ok(String::from("")),
        Err(e) => Ok(format!("Error sending AVC Command: {:?}", e)),
    }
}

async fn get_media<'a>(
    _args: &'a [&'a str],
    controller: &'a ControllerProxy,
) -> Result<String, Error> {
    match controller.get_media_attributes().await? {
        Ok(media) => Ok(format!("Media attributes: {:#?}", media)),
        Err(e) => Ok(format!("Error fetching media attributes: {:?}", e)),
    }
}

async fn get_play_status<'a>(
    _args: &'a [&'a str],
    controller: &'a ControllerProxy,
) -> Result<String, Error> {
    match controller.get_play_status().await? {
        Ok(status) => Ok(format!("Play status {:#?}", status)),
        Err(e) => Ok(format!("Error fetching play status {:?}", e)),
    }
}

fn parse_pas_ids(ids: Vec<&str>) -> Result<Vec<PlayerApplicationSettingAttributeId>, Error> {
    let mut attribute_ids = vec![];
    for attr_id in ids {
        match attr_id {
            "1" => attribute_ids.push(PlayerApplicationSettingAttributeId::Equalizer),
            "2" => attribute_ids.push(PlayerApplicationSettingAttributeId::RepeatStatusMode),
            "3" => attribute_ids.push(PlayerApplicationSettingAttributeId::ShuffleMode),
            "4" => attribute_ids.push(PlayerApplicationSettingAttributeId::ScanMode),
            _ => return Err(format_err!("Invalid attribute id.")),
        }
    }

    Ok(attribute_ids)
}

async fn get_player_application_settings<'a>(
    args: &'a [&'a str],
    controller: &'a ControllerProxy,
) -> Result<String, Error> {
    if args.len() > MAX_ATTRIBUTES as usize {
        return Ok(format!("usage: {}", Cmd::GetPlayerApplicationSettings.cmd_help()));
    }

    let ids = match parse_pas_ids(args.to_vec()) {
        Ok(ids) => ids,
        Err(_) => return Ok(format!("Invalid id in args {:?}", args)),
    };
    let get_pas_fut = controller.get_player_application_settings(&mut ids.into_iter());

    match get_pas_fut.await? {
        Ok(settings) => Ok(format!("Player application setting attribute value: {:#?}", settings)),
        Err(e) => Ok(format!("Error fetching player application attributes: {:?}", e)),
    }
}

async fn set_player_application_settings<'a>(
    _args: &'a [&'a str],
    controller: &'a ControllerProxy,
) -> Result<String, Error> {
    // Send canned response to AVRCP with Equalizer off.
    let mut settings = fidl_avrcp::PlayerApplicationSettings::new_empty();
    settings.equalizer = Some(fidl_avrcp::Equalizer::Off);

    match controller.set_player_application_settings(settings).await? {
        Ok(set_settings) => Ok(format!("Set settings with: {:?}", set_settings)),
        Err(e) => Ok(format!("Error in set settings {:?}", e)),
    }
}

async fn get_events_supported<'a>(
    _args: &'a [&'a str],
    controller: &'a ControllerExtProxy,
) -> Result<String, Error> {
    match controller.get_events_supported().await? {
        Ok(events) => Ok(format!("Supported events: {:#?}", events)),
        Err(e) => Ok(format!("Error fetching supported events: {:?}", e)),
    }
}

async fn send_raw_vendor<'a>(
    args: &'a [&'a str],
    controller: &'a ControllerExtProxy,
) -> Result<String, Error> {
    if args.len() < 2 {
        return Ok(format!("usage: {}", Cmd::SendRawVendorCommand.cmd_help()));
    }

    match parse_raw_packet(args) {
        Ok((pdu_id, buf)) => {
            eprintln!("Sending {:#?}", buf);

            match controller.send_raw_vendor_dependent_command(pdu_id, &mut buf.into_iter()).await?
            {
                Ok(response) => Ok(format!("response: {:#?}", response)),
                Err(e) => Ok(format!("Error sending raw dependent command: {:?}", e)),
            }
        }
        Err(message) => Ok(message),
    }
}

fn parse_raw_packet(args: &[&str]) -> Result<(u8, Vec<u8>), String> {
    let pdu_id;

    if args[0].starts_with("0x") || args[0].starts_with("0X") {
        if let Ok(hex) = Vec::from_hex(&args[0][2..]) {
            if hex.len() < 1 {
                return Err(format!("invalid pdu_id {}", args[0]));
            }
            pdu_id = hex[0];
        } else {
            return Err(format!("invalid pdu_id {}", args[0]));
        }
    } else {
        if let Ok(b) = args[0].parse::<u8>() {
            pdu_id = b;
        } else {
            return Err(format!("invalid pdu_id {}", args[0]));
        }
    }

    let byte_string = args[1..].join(" ").replace(",", " ");

    let bytes = byte_string.split(" ");

    let mut buf = vec![];
    for b in bytes {
        let b = b.trim();
        if b.eq("") {
            continue;
        } else if b.starts_with("0x") || b.starts_with("0X") {
            if let Ok(hex) = Vec::from_hex(&b[2..]) {
                buf.extend_from_slice(&hex[..]);
            } else {
                return Err(format!("invalid hex string at {}", b));
            }
        } else {
            if let Ok(hex) = Vec::from_hex(&b[..]) {
                buf.extend_from_slice(&hex[..]);
            } else {
                return Err(format!("invalid hex string at {}", b));
            }
        }
    }

    Ok((pdu_id, buf))
}

async fn set_volume<'a>(
    args: &'a [&'a str],
    controller: &'a ControllerProxy,
) -> Result<String, Error> {
    let volume;
    if let Ok(val) = args[0].parse::<u8>() {
        if val > 127 {
            return Ok(format!("invalid volume range {}", args[0]));
        }
        volume = val;
    } else {
        return Ok(format!("unable to parse volume {}", args[0]));
    }

    match controller.set_absolute_volume(volume).await? {
        Ok(set_volume) => Ok(format!("Volume set to: {:?}", set_volume)),
        Err(e) => Ok(format!("Error setting volume: {:?}", e)),
    }
}

async fn is_connected<'a>(
    _args: &'a [&'a str],
    controller: &'a ControllerExtProxy,
) -> Result<String, Error> {
    match controller.is_connected().await {
        Ok(status) => Ok(format!("Is Connected: {}", status)),
        Err(e) => Ok(format!("Error fetching supported events: {:?}", e)),
    }
}

/// Handle a single raw input command from a user and indicate whether the command should
/// result in continuation or breaking of the read evaluate print loop.
async fn handle_cmd<'a>(
    controller: &'a ControllerProxy,
    test_controller: &'a ControllerExtProxy,
    line: String,
) -> Result<ReplControl, Error> {
    let components: Vec<_> = line.trim().split_whitespace().collect();
    if let Some((raw_cmd, args)) = components.split_first() {
        let cmd = raw_cmd.parse();
        let res = match cmd {
            Ok(Cmd::AvcCommand) => send_passthrough(args, &controller).await,
            Ok(Cmd::GetMediaAttributes) => get_media(args, &controller).await,
            Ok(Cmd::GetPlayStatus) => get_play_status(args, &controller).await,
            Ok(Cmd::GetPlayerApplicationSettings) => {
                get_player_application_settings(args, &controller).await
            }
            Ok(Cmd::SetPlayerApplicationSettings) => {
                set_player_application_settings(args, &controller).await
            }
            Ok(Cmd::SendRawVendorCommand) => send_raw_vendor(args, &test_controller).await,
            Ok(Cmd::SupportedEvents) => get_events_supported(args, &test_controller).await,
            Ok(Cmd::SetVolume) => set_volume(args, &controller).await,
            Ok(Cmd::IsConnected) => is_connected(args, &test_controller).await,
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

async fn controller_listener(
    controller: &ControllerProxy,
    mut stream: ControllerEventStream,
) -> Result<(), Error> {
    while let Some(evt) = stream.try_next().await? {
        print!("{}", CLEAR_LINE);
        match evt {
            ControllerEvent::OnNotification { timestamp, notification } => {
                if let Some(value) = notification.pos {
                    println!("Pos event: {:?} {:?}", timestamp, value);
                } else if let Some(value) = notification.status {
                    println!("Status event: {:?} {:?}", timestamp, value);
                } else if let Some(value) = notification.track_id {
                    println!("Track event: {:?} {:?}", timestamp, value);
                } else if let Some(value) = notification.volume {
                    println!("Volume event: {:?} {:?}", timestamp, value);
                } else {
                    println!("Other event: {:?} {:?}", timestamp, notification);
                }
                controller.notify_notification_handled()?;
            }
        }
    }
    Ok(())
}

/// REPL execution
async fn run_repl<'a>(
    controller: &'a ControllerProxy,
    test_controller: &'a ControllerExtProxy,
) -> Result<(), Error> {
    // `cmd_stream` blocks on input in a separate thread and passes commands and acks back to
    // the main thread via async channels.
    let (mut commands, mut acks) = cmd_stream();
    loop {
        if let Some(cmd) = commands.next().await {
            match handle_cmd(controller, test_controller, cmd).await {
                Ok(ReplControl::Continue) => {}
                Ok(ReplControl::Break) => {
                    println!("\n");
                    break;
                }
                Err(e) => {
                    println!("Error handling command: {}", e);
                }
            }
        } else {
            break;
        }
        acks.send(()).await?;
    }
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let opt = Opt::from_args();

    let device_id = &opt.device.replace("-", "").to_lowercase();

    // Connect to test controller service first so we fail early if it's not available.

    let test_avrcp_svc = connect_to_service::<PeerManagerExtMarker>()
        .context("Failed to connect to Bluetooth Test AVRCP interface")?;

    // Create a channel for our Request<TestController> to live
    let (t_client, t_server) =
        create_endpoints::<ControllerExtMarker>().expect("Error creating Test Controller endpoint");

    let _status = test_avrcp_svc.get_controller_for_target(&device_id.as_str(), t_server).await?;
    eprintln!(
        "Test controller obtained to device \"{device}\" AVRCP remote target service",
        device = &device_id,
    );

    // Connect to avrcp controller service.

    let avrcp_svc = connect_to_service::<PeerManagerMarker>()
        .context("Failed to connect to Bluetooth AVRCP interface")?;

    // Create a channel for our Request<Controller> to live
    let (c_client, c_server) =
        create_endpoints::<ControllerMarker>().expect("Error creating Controller endpoint");

    let _status = avrcp_svc.get_controller_for_target(&device_id.as_str(), c_server).await?;
    eprintln!(
        "Controller obtained to device \"{device}\" AVRCP remote target service",
        device = &device_id,
    );

    // setup repl

    let controller = c_client.into_proxy().expect("error obtaining controller client proxy");
    let test_controller =
        t_client.into_proxy().expect("error obtaining test controller client proxy");

    let evt_stream = controller.clone().take_event_stream();

    // set controller event filter to ones we support.
    let _ = controller.set_notification_filter(
        Notifications::PlaybackStatus
            | Notifications::Track
            | Notifications::TrackPos
            | Notifications::Volume,
        1,
    )?;

    let event_fut = controller_listener(&controller, evt_stream).fuse();
    let repl_fut = run_repl(&controller, &test_controller).fuse();
    pin_mut!(event_fut);
    pin_mut!(repl_fut);

    // These futures should only return when something fails.
    select! {
        result = event_fut => {
            eprintln!(
                "Service connection returned {status:?}", status = result);
        },
        _ = repl_fut => {}
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_raw_packet_parsing_01() {
        assert_eq!(parse_raw_packet(&["0x40", "0xaaaa"]), Ok((0x40, vec![0xaa, 0xaa])));
    }
    #[test]
    fn test_raw_packet_parsing_02() {
        assert_eq!(parse_raw_packet(&["0x40", "0xaa", "0xaa"]), Ok((0x40, vec![0xaa, 0xaa])));
    }
    #[test]
    fn test_raw_packet_parsing_03() {
        assert_eq!(
            parse_raw_packet(&["0x40", "0xAa", "0XaA", "0x1234"]),
            Ok((0x40, vec![0xaa, 0xaa, 0x12, 0x34]))
        );
    }
    #[test]
    fn test_raw_packet_parsing_04() {
        assert_eq!(
            parse_raw_packet(&["40", "0xaa", "0xaa", "0x1234"]),
            Ok((40, vec![0xaa, 0xaa, 0x12, 0x34]))
        );
    }
    #[test]
    fn test_raw_packet_parsing_05() {
        assert_eq!(
            parse_raw_packet(&["40", "aa", "aa", "1234"]),
            Ok((40, vec![0xaa, 0xaa, 0x12, 0x34]))
        );
    }
    #[test]
    fn test_raw_packet_parsing_06() {
        assert_eq!(parse_raw_packet(&["40", "aa,aa,1234"]), Ok((40, vec![0xaa, 0xaa, 0x12, 0x34])));
    }
    #[test]
    fn test_raw_packet_parsing_07() {
        assert_eq!(
            parse_raw_packet(&["40", "0xaa, 0xaa,    0x1234"]),
            Ok((40, vec![0xaa, 0xaa, 0x12, 0x34]))
        );
    }
    #[test]
    fn test_raw_packet_parsing_08_err_pdu_overflow() {
        assert_eq!(
            parse_raw_packet(&["300", "0xaa, 0xaa,    0x1234"]),
            Err("invalid pdu_id 300".to_string())
        );
    }
    #[test]
    fn test_raw_packet_parsing_09_err_invalid_hex_long() {
        assert_eq!(
            parse_raw_packet(&["40", "0xzz, 0xaa,    0x1234"]),
            Err("invalid hex string at 0xzz".to_string())
        );
    }
    #[test]
    fn test_raw_packet_parsing_10_err_invalid_hex_short() {
        assert_eq!(
            parse_raw_packet(&["40", "zz, 0xaa, 0xqqqq"]),
            Err("invalid hex string at zz".to_string())
        );
    }
    #[test]
    fn test_raw_packet_parsing_11_err_invalid_hex() {
        assert_eq!(
            parse_raw_packet(&["40", "ab, 0xaa, 0xqqqq"]),
            Err("invalid hex string at 0xqqqq".to_string())
        );
    }

    #[test]
    fn test_parse_pas_id_success() {
        let ids = vec!["1", "2", "3"];
        let result = parse_pas_ids(ids);
        assert!(result.is_ok());
        let result = result.unwrap();
        assert_eq!(
            result,
            vec![
                PlayerApplicationSettingAttributeId::Equalizer,
                PlayerApplicationSettingAttributeId::RepeatStatusMode,
                PlayerApplicationSettingAttributeId::ShuffleMode
            ]
        );
    }

    #[test]
    fn test_parse_pas_id_error() {
        let ids = vec!["fake", "id", "1"];
        let result = parse_pas_ids(ids);
        assert!(result.is_err());
    }

    #[test]
    fn test_parse_pas_id_invalid_id_error() {
        let ids = vec!["1", "2", "5"];
        let result = parse_pas_ids(ids);
        assert!(result.is_err());
    }
}
