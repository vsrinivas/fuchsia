// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};
use fidl_fuchsia_update::{
    Initiator, ManagerMarker, MonitorEvent, MonitorMarker, MonitorProxy, Options, State,
};
use fidl_fuchsia_update_channelcontrol::ChannelControlMarker;
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, launcher};
use futures::prelude::*;

mod args;

fn print_state(state: State) {
    if let Some(state) = state.state {
        println!("State: {:?}", state);
    }
    if let Some(version) = state.version_available {
        println!("Version available: {}", version);
    }
}

async fn monitor_state(monitor: MonitorProxy) -> Result<(), Error> {
    let mut stream = monitor.take_event_stream();
    while let Some(event) = stream.try_next().await? {
        match event {
            MonitorEvent::OnState { state } => {
                print_state(state);
            }
        }
    }
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Launch the server and connect to the omaha client service.
    let args::Update { server_url, cmd } = argh::from_env();
    let launcher = launcher().context("Failed to open launcher service")?;
    let app =
        launch(&launcher, server_url, None).context("Failed to launch omaha client service")?;
    match cmd {
        args::Command::Channel(args::Channel { cmd }) => {
            let channel_control = app
                .connect_to_service::<ChannelControlMarker>()
                .context("Failed to connect to channel control service")?;

            match cmd {
                args::channel::Command::Get(_) => {
                    let channel = channel_control.get_current().await?;
                    println!("current channel: {}", channel);
                }
                args::channel::Command::Target(_) => {
                    let channel = channel_control.get_target().await?;
                    println!("target channel: {}", channel);
                }
                args::channel::Command::Set(args::channel::Set { channel }) => {
                    channel_control.set_target(&channel).await?;
                }
                args::channel::Command::List(_) => {
                    let channels = channel_control.get_target_list().await?;
                    if channels.is_empty() {
                        println!("known channels list is empty.");
                    } else {
                        println!("known channels:");
                        for channel in channels {
                            println!("{}", channel);
                        }
                    }
                }
            }
        }
        args::Command::State(_) | args::Command::CheckNow(_) | args::Command::Monitor(_) => {
            let omaha_client = app
                .connect_to_service::<ManagerMarker>()
                .context("Failed to connect to omaha client manager service")?;

            match cmd {
                args::Command::State(_) => {
                    let state = omaha_client.get_state().await?;
                    print_state(state);
                }
                args::Command::CheckNow(args::CheckNow { service_initiated, monitor }) => {
                    let options = Options {
                        initiator: Some(if service_initiated {
                            Initiator::Service
                        } else {
                            Initiator::User
                        }),
                    };
                    if monitor {
                        let (client_proxy, server_end) =
                            fidl::endpoints::create_proxy::<MonitorMarker>()?;
                        let result = omaha_client.check_now(options, Some(server_end)).await?;
                        println!("Check started result: {:?}", result);
                        monitor_state(client_proxy).await?;
                    } else {
                        let result = omaha_client.check_now(options, None).await?;
                        println!("Check started result: {:?}", result);
                    }
                }
                args::Command::Monitor(_) => {
                    let (client_proxy, server_end) =
                        fidl::endpoints::create_proxy::<MonitorMarker>()?;
                    omaha_client.add_monitor(server_end)?;
                    monitor_state(client_proxy).await?;
                }
                _ => {}
            }
        }
    }
    Ok(())
}
