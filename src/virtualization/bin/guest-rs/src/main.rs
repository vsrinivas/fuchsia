// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {anyhow::Error, argh::FromArgs, fuchsia_async as fasync};

mod balloon;
mod launch;
mod list;
mod services;
mod socat;

#[derive(FromArgs, PartialEq, Debug)]
/// Top-level command.
struct GuestOptions {
    #[argh(subcommand)]
    nested: SubCommands,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum SubCommands {
    One(Launch),
    Two(Balloon),
    Three(BalloonStats),
    Four(Serial),
    Five(List),
    Six(Socat),
    Seven(SocatListen),
    Eight(Vsh),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Launch a guest image. Usage: guest-rs launch --package <package> [--vmm_args <args>]
#[argh(subcommand, name = "launch")]
struct Launch {
    #[argh(option)]
    /// package name to launch e.g. 'zircon_guest'.
    package: String,
    /// arguments to configure guest image with.
    #[argh(option)]
    vmm_args: Vec<String>,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Modify the size of a memory balloon. Usage: guest-rs balloon --env_id <id> --cid <id> --num_pages <num>
#[argh(subcommand, name = "balloon")]
struct Balloon {
    #[argh(option)]
    /// environment id where guest lives.
    env_id: u32,
    #[argh(option)]
    /// context id of guest.
    cid: u32,
    #[argh(option)]
    /// number of pages guest balloon will have after use.
    num_pages: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// See the stats of a guest's memory balloon. Usage: guest-rs balloon-stats --env_id <id> --cid <id>
#[argh(subcommand, name = "balloon-stats")]
struct BalloonStats {
    #[argh(option)]
    /// environment id where guest lives.
    env_id: u32,
    #[argh(option)]
    /// context id of guest.
    cid: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Access the serial output for a guest. Usage: guest-rs serial --env_id <id> --cid <id>
#[argh(subcommand, name = "serial")]
struct Serial {
    #[argh(option)]
    /// environment id where guest lives.
    env_id: u32,
    #[argh(option)]
    /// context id of guest.
    cid: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// List existing guest environments. Usage: guest-rs list
#[argh(subcommand, name = "list")]
struct List {}

#[derive(FromArgs, PartialEq, Debug)]
/// Create a socat connection on the specified port. Usage: guest-rs socat --env_id <id> --cid <id> --port <num>
#[argh(subcommand, name = "socat")]
struct Socat {
    #[argh(option)]
    /// environment id where guest lives.
    env_id: u32,
    #[argh(option)]
    /// context id of guest.
    cid: u32,
    #[argh(option)]
    /// port for listeners to connect on.
    port: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Listen through socat on the specified port. Usage: guest-rs socat-listen --env_id <id> --host_port <num>
#[argh(subcommand, name = "socat-listen")]
struct SocatListen {
    #[argh(option)]
    /// environment id of host.
    env_id: u32,
    #[argh(option)]
    /// port number of host (see `guest-rs socat`)
    host_port: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Create virtual shell for a guest or connect via virtual shell. Usage: guest-rs [--env_id <id> [--cid <id> [--port [num]]]] [--args <args>]
#[argh(subcommand, name = "vsh")]
struct Vsh {
    #[argh(option)]
    /// optional environment id of host.
    env_id: Option<u32>,
    #[argh(option)]
    /// optional context id of vsh to connect to.
    cid: Option<u32>,
    #[argh(option)]
    /// optional port of a vsh socket to connect to.
    port: Option<u32>,
    #[argh(option)]
    /// list of arguments to run non-interactively on launch.
    args: Vec<String>,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let options: GuestOptions = argh::from_env();

    match options.nested {
        SubCommands::One(launch_args) => {
            let guest = launch::GuestLaunch::new(launch_args.package, launch_args.vmm_args).await?;
            guest.run().await
        }
        SubCommands::Two(balloon_args) => {
            let balloon_controller =
                balloon::connect_to_balloon_controller(balloon_args.env_id, balloon_args.cid)
                    .await?;
            let output =
                balloon::handle_balloon(balloon_controller, balloon_args.num_pages).await?;
            println!("{}", output);
            Ok(())
        }
        SubCommands::Three(balloon_stat_args) => {
            let balloon_controller = balloon::connect_to_balloon_controller(
                balloon_stat_args.env_id,
                balloon_stat_args.cid,
            )
            .await?;
            let output = balloon::handle_balloon_stats(balloon_controller).await?;
            println!("{}", output);
            Ok(())
        }
        SubCommands::Five(..) => {
            let manager = services::connect_to_manager()?;
            let output = list::handle_list(manager).await?;
            println!("{}", output);
            Ok(())
        }
        SubCommands::Six(socat_args) => {
            let vsock_endpoint = socat::connect_to_vsock_endpoint(socat_args.env_id).await?;
            socat::handle_socat(vsock_endpoint, socat_args.cid, socat_args.port).await
        }
        SubCommands::Seven(socat_listen_args) => {
            let vsock_endpoint = socat::connect_to_vsock_endpoint(socat_listen_args.env_id).await?;
            socat::handle_socat_listen(vsock_endpoint, socat_listen_args.host_port).await
        }
        _ => unimplemented!(), // TODO(fxbug.dev/89427): Implement guest tool
    }
}
