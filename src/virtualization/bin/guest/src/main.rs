// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::arguments::*,
    anyhow::{anyhow, Error},
    fuchsia_async as fasync,
};

mod arguments;
mod balloon;
mod launch;
mod list;
mod serial;
mod services;
mod socat;

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
                balloon::connect_to_balloon_controller(
                    balloon_args.env_id.ok_or(anyhow!("Missing required 'env_id' argument"))?,
                    balloon_args.cid.ok_or(anyhow!("Missing required 'cid' argument"))?,
                )
                .await?
            } else {
                balloon::connect_to_balloon_controller_cfv2(balloon_args.guest_type).await?
            };
            let output =
                balloon::handle_balloon(balloon_controller, balloon_args.num_pages).await?;
            println!("{}", output);
            Ok(())
        }
        SubCommands::BalloonStats(balloon_stat_args) => {
            let balloon_controller = if cfg!(feature = "USE_CFV1") {
                balloon::connect_to_balloon_controller(
                    balloon_stat_args
                        .env_id
                        .ok_or(anyhow!("Missing required 'env_id' argument"))?,
                    balloon_stat_args.cid.ok_or(anyhow!("Missing required 'cid' argument"))?,
                )
                .await?
            } else {
                balloon::connect_to_balloon_controller_cfv2(balloon_stat_args.guest_type).await?
            };
            let output = balloon::handle_balloon_stats(balloon_controller).await?;
            println!("{}", output);
            Ok(())
        }
        SubCommands::Serial(serial_args) => {
            let guest = if cfg!(feature = "USE_CFV1") {
                services::connect_to_guest(
                    serial_args.env_id.ok_or(anyhow!("Missing required 'env_id' argument"))?,
                    serial_args.cid.ok_or(anyhow!("Missing required 'cid' argument"))?,
                )?
            } else {
                services::connect_to_guest_cfv2(serial_args.guest_type).await?
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
                socat::connect_to_vsock_endpoint(
                    socat_args.env_id.ok_or(anyhow!("Missing required 'env_id' argument"))?,
                )
                .await?
            } else {
                socat::connect_to_vsock_endpoint_cfv2(socat_args.guest_type).await?
            };
            socat::handle_socat(vsock_endpoint, socat_args.port).await
        }
        SubCommands::SocatListen(socat_listen_args) => {
            let vsock_endpoint = if cfg!(feature = "USE_CFV1") {
                socat::connect_to_vsock_endpoint(
                    socat_listen_args
                        .env_id
                        .ok_or(anyhow!("Missing required 'env_id' argument"))?,
                )
                .await?
            } else {
                socat::connect_to_vsock_endpoint_cfv2(socat_listen_args.guest_type).await?
            };
            socat::handle_socat_listen(vsock_endpoint, socat_listen_args.host_port).await
        }
        _ => unimplemented!(), // TODO(fxbug.dev/89427): Implement guest tool
    }
}
