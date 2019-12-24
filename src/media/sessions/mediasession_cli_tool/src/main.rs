// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow;
use argh::FromArgs;
use fidl::encoding::Decodable;
use fidl::endpoints::create_endpoints;
use fidl_fuchsia_media_sessions2::*;
use fuchsia_async as fasync;
use fuchsia_component::client;
use futures::prelude::*;

#[derive(FromArgs)]
/// A tool for inspecting and controlling registered media sessions.
struct Invocation {
    #[argh(subcommand)]
    command: Command,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum Command {
    Ls(Ls),
    Info(Info),
    Control(Control),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "ls")]
/// List registered session states as they arrive.
struct Ls {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "info")]
/// Announce info about a session as it arrives.
struct Info {
    #[argh(option, short = 'i')]
    /// id of a session
    session_id: u64,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "control")]
/// Issue a control command to a session.
struct Control {
    #[argh(option, short = 'i')]
    /// id of a session
    session_id: u64,
    #[argh(subcommand)]
    command: ControlCommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum ControlCommand {
    Play(Play),
    Pause(Pause),
    Stop(Stop),
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "play")]
/// Initiates playback.
struct Play {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "pause")]
/// Pauses playback.
struct Pause {}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "stop")]
/// Tears down the session.
struct Stop {}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), anyhow::Error> {
    let invocation: Invocation = argh::from_env();
    let discovery = client::connect_to_service::<DiscoveryMarker>()?;
    let (watcher_client, watcher_server) = create_endpoints()?;
    let mut watcher_requests = watcher_server.into_stream()?;

    match invocation.command {
        Command::Ls(_) => {
            discovery.watch_sessions(WatchOptions::new_empty(), watcher_client)?;
            while let Some((id, delta, responder)) = watcher_requests
                .try_next()
                .await?
                .and_then(SessionsWatcherRequest::into_session_updated)
            {
                responder.send()?;
                println!(
                    "[{}] State: {:?}",
                    id,
                    delta.player_status.and_then(|ps| ps.player_state)
                );
            }
        }
        Command::Info(info) => {
            discovery.watch_sessions(WatchOptions::new_empty(), watcher_client)?;
            while let Some((id, delta, responder)) = watcher_requests
                .try_next()
                .await?
                .and_then(SessionsWatcherRequest::into_session_updated)
            {
                if id == info.session_id {
                    println!("{:#?}", delta);
                    break;
                } else {
                    responder.send()?;
                }
            }
        }
        Command::Control(control) => {
            let (session_client, session_request) = create_endpoints()?;
            let proxy: SessionControlProxy = session_client.into_proxy()?;
            match control.command {
                ControlCommand::Play(_) => proxy.play()?,
                ControlCommand::Pause(_) => proxy.pause()?,
                ControlCommand::Stop(_) => proxy.stop()?,
            }
            discovery.connect_to_session(control.session_id, session_request)?;
        }
    }

    Ok(())
}
