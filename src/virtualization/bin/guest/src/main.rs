// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {anyhow::Error, argh::FromArgs, fuchsia_async as fasync};

mod arguments;
mod balloon;
mod launch;
mod list;
mod serial;
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
    Launch(arguments::LaunchArgs),
    Balloon(BalloonArgs),
    BalloonStats(BalloonStatsArgs),
    Serial(SerialArgs),
    List(ListArgs),
    Socat(SocatArgs),
    SocatListen(SocatListenArgs),
    Vsh(VshArgs),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Modify the size of a memory balloon. Usage: guest balloon env-id cid num-pages
#[argh(subcommand, name = "balloon")]
struct BalloonArgs {
    #[argh(positional)]
    /// environment id where guest lives.
    env_id: u32,
    #[argh(positional)]
    /// context id of guest.
    cid: u32,
    #[argh(option)]
    /// type of the guest
    guest_type: Option<arguments::GuestType>,
    #[argh(positional)]
    /// number of pages guest balloon will have after use.
    num_pages: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// See the stats of a guest's memory balloon. Usage: guest balloon-stats env-id cid
#[argh(subcommand, name = "balloon-stats")]
struct BalloonStatsArgs {
    #[argh(positional)]
    /// environment id where guest lives.
    env_id: u32,
    #[argh(positional)]
    /// context id of guest.
    cid: u32,
    #[argh(option)]
    /// type of the guest
    guest_type: Option<arguments::GuestType>,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Access the serial output for a guest. Usage: guest serial env-id cid
#[argh(subcommand, name = "serial")]
struct SerialArgs {
    #[argh(positional)]
    /// environment id where guest lives.
    env_id: u32,
    #[argh(positional)]
    /// context id of guest.
    cid: u32,
    #[argh(option)]
    /// type of the guest
    guest_type: Option<arguments::GuestType>,
}

#[derive(FromArgs, PartialEq, Debug)]
/// List existing guest environments. Usage: guest list
#[argh(subcommand, name = "list")]
struct ListArgs {}

#[derive(FromArgs, PartialEq, Debug)]
/// Create a socat connection on the specified port. Usage: guest socat env-id port
#[argh(subcommand, name = "socat")]
struct SocatArgs {
    #[argh(positional)]
    /// environment id where guest lives.
    env_id: u32,
    #[argh(option)]
    /// type of the guest
    guest_type: Option<arguments::GuestType>,
    #[argh(option)]
    /// port for listeners to connect on.
    port: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Listen through socat on the specified port. Usage: guest socat-listen env-id host-port
#[argh(subcommand, name = "socat-listen")]
struct SocatListenArgs {
    #[argh(positional)]
    /// environment id of host.
    env_id: u32,
    #[argh(option)]
    /// type of the guest
    guest_type: Option<arguments::GuestType>,
    #[argh(option)]
    /// port number of host (see `guest socat`)
    host_port: u32,
}

#[derive(FromArgs, PartialEq, Debug)]
/// Create virtual shell for a guest or connect via virtual shell. Usage: guest vsh [env_id [cid [port]]] [--args <arg>]
#[argh(subcommand, name = "vsh")]
struct VshArgs {
    #[argh(option)]
    /// positional environment id of host.
    env_id: Option<u32>,
    #[argh(option)]
    /// positional context id of vsh to connect to.
    cid: Option<u32>,
    #[argh(option)]
    /// positional port of a vsh socket to connect to.
    port: Option<u32>,
    #[argh(option)]
    /// list of arguments to run non-interactively on launch.
    args: Vec<String>,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let options: GuestOptions = argh::from_env();

    match options.nested {
        SubCommands::Launch(launch_args) => {
            let guest_config = launch::parse_vmm_args(&launch_args);

            let guest = if cfg!(feature = "USE_CFV1") {
                launch::GuestLaunch::new(launch_args.guest_type, guest_config).await?
            } else {
                launch::GuestLaunch::new_cfv2(launch_args.guest_type, guest_config).await?
            };
            guest.run().await
        }
        SubCommands::Balloon(balloon_args) => {
            let balloon_controller = if cfg!(feature = "USE_CFV1") {
                balloon::connect_to_balloon_controller(balloon_args.env_id, balloon_args.cid)
                    .await?
            } else {
                balloon::connect_to_balloon_controller_cfv2(balloon_args.guest_type.unwrap())
                    .await?
            };
            let output =
                balloon::handle_balloon(balloon_controller, balloon_args.num_pages).await?;
            println!("{}", output);
            Ok(())
        }
        SubCommands::BalloonStats(balloon_stat_args) => {
            let balloon_controller = if cfg!(feature = "USE_CFV1") {
                balloon::connect_to_balloon_controller(
                    balloon_stat_args.env_id,
                    balloon_stat_args.cid,
                )
                .await?
            } else {
                balloon::connect_to_balloon_controller_cfv2(balloon_stat_args.guest_type.unwrap())
                    .await?
            };
            let output = balloon::handle_balloon_stats(balloon_controller).await?;
            println!("{}", output);
            Ok(())
        }
        SubCommands::Serial(serial_args) => {
            let guest = if cfg!(feature = "USE_CFV1") {
                services::connect_to_guest(serial_args.env_id, serial_args.cid)?
            } else {
                services::connect_to_guest_cfv2(serial_args.guest_type.unwrap()).await?
            };
            serial::handle_serial(guest).await
        }
        SubCommands::List(..) => {
            if cfg!(feature = "USE_CFV1") {
                let manager = services::connect_to_manager()?;
                let output = list::handle_list(manager).await?;
                println!("{}", output);
            } else {
                let supported_guests = vec![
                    arguments::GuestType::Zircon,
                    arguments::GuestType::Debian,
                    arguments::GuestType::Termina,
                ];
                let mut managers = Vec::new();
                for guest_type in supported_guests {
                    managers.push((
                        guest_type.to_string(),
                        services::connect_to_manager_cfv2(guest_type)?,
                    ));
                }
                let output = list::handle_list_cfv2(&managers).await?;
                println!("{}", output);
            }
            Ok(())
        }
        SubCommands::Socat(socat_args) => {
            let vsock_endpoint = if cfg!(feature = "USE_CFV1") {
                socat::connect_to_vsock_endpoint(socat_args.env_id).await?
            } else {
                socat::connect_to_vsock_endpoint_cfv2(socat_args.guest_type.unwrap()).await?
            };
            socat::handle_socat(vsock_endpoint, socat_args.port).await
        }
        SubCommands::SocatListen(socat_listen_args) => {
            let vsock_endpoint = if cfg!(feature = "USE_CFV1") {
                socat::connect_to_vsock_endpoint(socat_listen_args.env_id).await?
            } else {
                socat::connect_to_vsock_endpoint_cfv2(socat_listen_args.guest_type.unwrap()).await?
            };
            socat::handle_socat_listen(vsock_endpoint, socat_listen_args.host_port).await
        }
        _ => unimplemented!(), // TODO(fxbug.dev/89427): Implement guest tool
    }
}
