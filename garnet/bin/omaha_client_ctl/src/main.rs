// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use failure::{Error, ResultExt};
use fidl_fuchsia_omaha_client::OmahaClientConfigurationMarker;
use fidl_fuchsia_update::{
    Initiator, ManagerMarker, MonitorEvent, MonitorMarker, MonitorProxy, Options, State,
};
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, launcher};
use fuchsia_zircon as zx;
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
    while let Some(event) = await!(stream.try_next())? {
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
        // fuchsia.omaha.client OmahaClientConfiguration protocol:
        GetChannel,
        SetChannel {
            channel: String,

            #[structopt(long = "no-factory-reset")]
            // Can't change default value for bool, it always defaults to false.
            no_factory_reset: bool,
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
        Command::GetChannel | Command::SetChannel { .. } => {
            let omaha_client = app
                .connect_to_service::<OmahaClientConfigurationMarker>()
                .context("Failed to connect to omaha client configuration service")?;

            match cmd {
                Command::GetChannel => {
                    let channel = await!(omaha_client.get_channel())?;
                    println!("channel: {}", channel);
                }
                Command::SetChannel { channel, no_factory_reset } => {
                    let status = await!(omaha_client.set_channel(&channel, !no_factory_reset))?;
                    zx::Status::ok(status)?;
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
                    let state = await!(omaha_client.get_state())?;
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
                        let result = await!(omaha_client.check_now(options, Some(server_end)))?;
                        println!("Check started result: {:?}", result);
                        await!(monitor_state(client_proxy))?;
                    } else {
                        let result = await!(omaha_client.check_now(options, None))?;
                        println!("Check started result: {:?}", result);
                    }
                }
                Command::Monitor => {
                    let (client_proxy, server_end) =
                        fidl::endpoints::create_proxy::<MonitorMarker>()?;
                    omaha_client.add_monitor(server_end)?;
                    await!(monitor_state(client_proxy))?;
                }
                _ => {}
            }
        }
    }
    Ok(())
}
