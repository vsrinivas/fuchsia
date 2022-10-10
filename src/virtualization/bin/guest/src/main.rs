// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::arguments::*,
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_virtualization::LinuxManagerMarker,
    fuchsia_component::client::connect_to_protocol,
};

mod arguments;
mod balloon;
mod launch;
mod list;
mod serial;
mod services;
mod socat;
mod stop;
mod vsh;
mod vsockperf;
mod wipe;

#[fuchsia::main(logging_tags = ["guest"])]
async fn main() -> Result<(), Error> {
    let options: GuestOptions = argh::from_env();

    match options.nested {
        SubCommands::Launch(launch_args) => {
            let guest_config = launch::parse_vmm_args(&launch_args);
            let guest = launch::GuestLaunch::new(launch_args.guest_type, guest_config).await?;
            guest.run().await
        }
        SubCommands::Stop(stop_args) => stop::handle_stop(&stop_args).await,
        SubCommands::Balloon(balloon_args) => {
            let balloon_controller =
                balloon::connect_to_balloon_controller(balloon_args.guest_type).await?;
            let output =
                balloon::handle_balloon(balloon_controller, balloon_args.num_pages).await?;
            println!("{}", output);
            Ok(())
        }
        SubCommands::BalloonStats(balloon_stat_args) => {
            let balloon_controller =
                balloon::connect_to_balloon_controller(balloon_stat_args.guest_type).await?;
            let output = balloon::handle_balloon_stats(balloon_controller).await?;
            println!("{}", output);
            Ok(())
        }
        SubCommands::Serial(serial_args) => {
            let guest = services::connect(serial_args.guest_type).await?;
            serial::handle_serial(guest).await
        }
        SubCommands::List(list_args) => {
            let output = list::handle_list(&list_args).await?;
            println!("{}", output);
            Ok(())
        }
        SubCommands::Wipe(args) => {
            if args.guest_type != GuestType::Termina {
                return Err(anyhow!(
                    "Wipe is not supported for '{}'. Only 'termina' is supported",
                    args.guest_type
                ));
            }
            let linux_manager = connect_to_protocol::<LinuxManagerMarker>()
                .context("Failed to connect to LinuxManager")?;
            wipe::handle_wipe(linux_manager).await
        }
        SubCommands::VsockPerf(args) => match args.guest_type {
            GuestType::Debian => vsockperf::run_micro_benchmark(args.guest_type).await,
            _ => Err(anyhow!("Vsock Perf is not supported for '{}'", args.guest_type)),
        },
        SubCommands::Socat(socat_args) => {
            let vsock_endpoint = socat::connect_to_vsock_endpoint(socat_args.guest_type).await?;
            socat::handle_socat(vsock_endpoint, socat_args.port).await
        }
        SubCommands::SocatListen(socat_listen_args) => {
            let vsock_endpoint =
                socat::connect_to_vsock_endpoint(socat_listen_args.guest_type).await?;
            socat::handle_socat_listen(vsock_endpoint, socat_listen_args.host_port).await
        }
        SubCommands::Vsh(vsh_args) => {
            // SAFETY: These unsafe helpers should only be called once as they take ownership of the
            // Stdin and Stdout file descriptors (and also assume that these FDs exceed the lifetime
            // of the returned object). An additional consequence is that those FDs will be closed
            // on drop, so we call this as early as possible.
            let mut stdin = unsafe { services::get_evented_stdio(services::Stdio::Stdin) };
            let mut stdout = unsafe { services::get_evented_stdio(services::Stdio::Stdout) };
            let mut stderr = unsafe { services::get_evented_stdio(services::Stdio::Stderr) };

            let termina_manager = services::connect_to_manager(arguments::GuestType::Termina)?;

            vsh::handle_vsh(
                &mut stdin,
                &mut stdout,
                &mut stderr,
                termina_manager,
                vsh_args.port,
                vsh_args.container,
                vsh_args.args,
            )
            .await
            .map(|exit_code| std::process::exit(exit_code))
        }
    }
}
