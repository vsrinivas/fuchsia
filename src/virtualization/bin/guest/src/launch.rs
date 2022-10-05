// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::arguments,
    crate::services,
    anyhow::{anyhow, Context, Error},
    fidl_fuchsia_virtualization::{GuestConfig, GuestMarker, GuestProxy},
    fuchsia_async as fasync,
};

pub fn parse_vmm_args(arguments: &arguments::LaunchArgs) -> GuestConfig {
    // FIDL requires we make a GuestConfig::EMPTY before trying to update fields
    let mut guest_config = GuestConfig::EMPTY;

    if !arguments.cmdline_add.is_empty() {
        guest_config.cmdline_add = Some(arguments.cmdline_add.clone())
    };

    guest_config.guest_memory = arguments.memory;
    guest_config.cpus = arguments.cpus;
    guest_config.default_net = arguments.default_net;
    guest_config.virtio_balloon = arguments.virtio_balloon;
    guest_config.virtio_console = arguments.virtio_console;
    guest_config.virtio_gpu = arguments.virtio_gpu;
    guest_config.virtio_rng = arguments.virtio_rng;
    guest_config.virtio_sound = arguments.virtio_sound;
    guest_config.virtio_sound_input = arguments.virtio_sound_input;
    guest_config.virtio_vsock = arguments.virtio_vsock;

    guest_config
}

pub struct GuestLaunch {
    guest: GuestProxy,
}

impl GuestLaunch {
    pub async fn new(guest_type: arguments::GuestType, config: GuestConfig) -> Result<Self, Error> {
        let (guest, guest_server_end) =
            fidl::endpoints::create_proxy::<GuestMarker>().context("Failed to create Guest")?;

        println!("Starting {}", guest_type.to_string());
        let manager = services::connect_to_manager(guest_type)?;
        let fidl_result = manager.launch(config, guest_server_end).await;
        if let Err(fidl::Error::ClientChannelClosed { .. }) = fidl_result {
            eprintln!("");
            eprintln!("Unable to connect to start the guest.");
            eprintln!("  Ensure you have the guest and core shards available on in your build:");
            eprintln!("      fx set ... \\");
            eprintln!("          --with-base {} \\", guest_type.gn_target_label());
            eprintln!(
                "          --args='core_realm_shards += [ \"{}\" ]'",
                guest_type.gn_core_shard_label()
            );
            eprintln!("");
            return Err(anyhow!("Unable to start guest: {}", fidl_result.unwrap_err()));
        }
        fidl_result?.map_err(|err| anyhow!("{:?}", err))?;
        Ok(GuestLaunch { guest })
    }

    pub async fn run(&self) -> Result<(), Error> {
        // Set up serial output (grab a zx_socket)
        // Returns a QueryResponseFuture containing ANOTHER future
        let guest_serial_response = self.guest.get_serial().await?;
        let guest_console_response =
            self.guest.get_console().await?.map_err(|err| anyhow!(format!("{:?}", err)))?;

        // Turn this stream socket into a a rust stream of data
        let guest_serial_sock = fasync::Socket::from_socket(guest_serial_response)?;

        let console = services::GuestConsole::new(guest_console_response)?;

        // SAFETY: This block is unsafe due to the use of `get_evented_stdout`
        // See services.rs for proper usage of these methods. This usage is
        // valid as it only calls these methods once (ie a safe usage)
        unsafe {
            let stdout = services::get_evented_stdout();

            let serial_output = async {
                futures::io::copy(guest_serial_sock, &mut &stdout)
                    .await
                    .map(|_| ())
                    .map_err(anyhow::Error::from)
            };

            futures::future::try_join(
                serial_output,
                console.run(&services::get_evented_stdin(), &stdout),
            )
            .await
            .map(|_| ())
            .map_err(anyhow::Error::from)
        }
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_virtualization::GuestError,
        fuchsia_zircon::{self as zx},
        futures::future::join,
        futures::StreamExt,
    };

    #[fasync::run_until_stalled(test)]
    async fn launch_invalid_console_returns_error() {
        let (guest_proxy, mut guest_stream) = create_proxy_and_stream::<GuestMarker>().unwrap();

        let guest = GuestLaunch { guest: guest_proxy };
        let (serial_launch_sock, _serial_server_sock) =
            zx::Socket::create(zx::SocketOpts::STREAM).unwrap();

        let server = async move {
            let serial_responder = guest_stream
                .next()
                .await
                .expect("Failed to read from stream")
                .expect("Failed to parse request")
                .into_get_serial()
                .expect("Unexpected call to Guest Proxy");
            serial_responder.send(serial_launch_sock).expect("Failed to send request to proxy");

            let console_responder = guest_stream
                .next()
                .await
                .expect("Failed to read from stream")
                .expect("Failed to parse request")
                .into_get_console()
                .expect("Unexpected call to Guest Proxy");
            console_responder
                .send(&mut Err(GuestError::DeviceNotPresent))
                .expect("Failed to send request to proxy");
        };

        let client = guest.run();
        let (_, client_res) = join(server, client).await;
        assert!(client_res.is_err());
    }
}
