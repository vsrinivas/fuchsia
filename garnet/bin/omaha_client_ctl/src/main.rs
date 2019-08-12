// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

use failure::{Error, ResultExt};
use fidl_fuchsia_update::{
    ChannelControlMarker, Initiator, ManagerMarker, MonitorEvent, MonitorMarker, MonitorProxy,
    Options, State,
};
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, launcher};
use futures::prelude::*;
use structopt::StructOpt;

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
    #[derive(Debug, StructOpt)]
    #[structopt(name = "omaha_client_ctl")]
    struct Opt {
        #[structopt(
            long = "server",
            help = "URL of omaha client server",
            default_value = "fuchsia-pkg://fuchsia.com/omaha_client#meta/omaha_client_service.cmx"
        )]
        server_url: String,

        #[structopt(subcommand)]
        cmd: Command,
    }
    #[derive(Debug, StructOpt)]
    #[structopt(rename_all = "kebab-case")]
    enum Command {
        // fuchsia.update ChannelControl protocol:
        /// Get the current channel.
        GetChannel,
        /// Get the target channel.
        GetTarget,
        /// Set the target channel.
        SetTarget {
            channel: String,
        },

        // fuchsia.update Manager protocol:
        GetState,
        CheckNow {
            /// The update check was initiated by a service, in the background.
            #[structopt(long = "service-initiated")]
            service_initiated: bool,

            /// Monitor for state update.
            #[structopt(long)]
            monitor: bool,
        },
        Monitor,
    }

    // Launch the server and connect to the omaha client service.
    let Opt { server_url, cmd } = Opt::from_args();
    let launcher = launcher().context("Failed to open launcher service")?;
    let app =
        launch(&launcher, server_url, None).context("Failed to launch omaha client service")?;
    match cmd {
        Command::GetChannel | Command::GetTarget | Command::SetTarget { .. } => {
            let channel_control = app
                .connect_to_service::<ChannelControlMarker>()
                .context("Failed to connect to channel control service")?;

            match cmd {
                Command::GetChannel => {
                    let channel = channel_control.get_channel().await?;
                    println!("current channel: {}", channel);
                }
                Command::GetTarget => {
                    let channel = channel_control.get_target().await?;
                    println!("target channel: {}", channel);
                }
                Command::SetTarget { channel } => {
                    channel_control.set_target(&channel).await?;
                }
                _ => {}
            }
        }
        Command::GetState | Command::CheckNow { .. } | Command::Monitor => {
            let omaha_client = app
                .connect_to_service::<ManagerMarker>()
                .context("Failed to connect to omaha client manager service")?;

            match cmd {
                Command::GetState => {
                    let state = omaha_client.get_state().await?;
                    print_state(state);
                }
                Command::CheckNow { service_initiated, monitor } => {
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
                Command::Monitor => {
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
