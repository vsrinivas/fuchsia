// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::arguments,
    crate::services,
    anyhow::anyhow,
    anyhow::{Context, Error},
    fidl_fuchsia_virtualization::{GuestConfig, GuestMarker, GuestProxy},
    fuchsia_async as fasync,
    fuchsia_zircon::{self as zx, sys},
    fuchsia_zircon_status as zx_status,
    lazy_static::lazy_static,
    std::convert::TryFrom,
};

lazy_static! {
    static ref DEFAULT_GUEST_MEM_SIZE: u64 =
        zx::system_get_physmem() - std::cmp::min(zx::system_get_physmem() / 2, 3 * (1 << 30));
}

pub fn parse_vmm_args(arguments: &arguments::LaunchArgs) -> GuestConfig {
    // FIDL requires we make a GuestConfig::EMPTY before trying to update fields
    let mut guest_config = GuestConfig::EMPTY;

    if !arguments.cmdline_add.is_empty() {
        guest_config.cmdline_add = Some(arguments.cmdline_add.clone())
    };
    guest_config.default_net = Some(arguments.default_net);
    guest_config.guest_memory = Some(arguments.memory.unwrap_or(*DEFAULT_GUEST_MEM_SIZE));
    // There are no assumptions made by this unsafe block; it is only unsafe due to FFI.
    guest_config.cpus = Some(
        arguments.cpus.unwrap_or(unsafe { u8::try_from(sys::zx_system_get_num_cpus()).unwrap() }),
    );
    guest_config.virtio_balloon = Some(arguments.virtio_balloon);
    guest_config.virtio_console = Some(arguments.virtio_console);
    guest_config.virtio_gpu = Some(arguments.virtio_gpu);
    guest_config.virtio_rng = Some(arguments.virtio_rng);
    guest_config.virtio_sound = Some(arguments.virtio_sound);
    guest_config.virtio_sound_input = Some(arguments.virtio_sound_input);
    guest_config.virtio_vsock = Some(arguments.virtio_vsock);

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
        let fidl_result = manager.launch_guest(config, guest_server_end).await;
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
        fidl_result?.map_err(zx_status::Status::from_raw)?;
        Ok(GuestLaunch { guest })
    }

    pub async fn run(&self) -> Result<(), Error> {
        // Set up serial output (grab a zx_socket)
        // Returns a QueryResponseFuture containing ANOTHER future
        let guest_serial_response = self
            .guest
            .get_serial()
            .await?
            .map_err(|status| anyhow!(zx_status::Status::from_raw(status)))?;
        let guest_console_response = self
            .guest
            .get_console()
            .await?
            .map_err(|status| anyhow!(zx_status::Status::from_raw(status)))?;

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
        assert_matches::assert_matches,
        fidl::endpoints::create_proxy_and_stream,
        fuchsia_zircon::{self as zx},
        futures::future::join,
        futures::StreamExt,
    };

    #[fasync::run_until_stalled(test)]
    async fn launch_invalid_serial_returns_error() {
        let (guest_proxy, mut guest_stream) = create_proxy_and_stream::<GuestMarker>().unwrap();

        let guest = GuestLaunch { guest: guest_proxy };

        let server = async move {
            let serial_responder = guest_stream
                .next()
                .await
                .expect("Failed to read from stream")
                .expect("Failed to parse request")
                .into_get_serial()
                .expect("Unexpected call to Guest Proxy");
            serial_responder
                .send(&mut Err(zx_status::Status::INTERNAL.into_raw()))
                .expect("Failed to send request to proxy");
        };

        let client = guest.run();
        let (_, client_res) = join(server, client).await;
        assert_matches!(client_res.unwrap_err().downcast(), Ok(zx_status::Status::INTERNAL));
    }

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
            serial_responder
                .send(&mut Ok(serial_launch_sock))
                .expect("Failed to send request to proxy");

            let console_responder = guest_stream
                .next()
                .await
                .expect("Failed to read from stream")
                .expect("Failed to parse request")
                .into_get_console()
                .expect("Unexpected call to Guest Proxy");
            console_responder
                .send(&mut Err(zx_status::Status::INTERNAL.into_raw()))
                .expect("Failed to send request to proxy");
        };

        let client = guest.run();
        let (_, client_res) = join(server, client).await;
        assert_matches!(client_res.unwrap_err().downcast(), Ok(zx_status::Status::INTERNAL));
    }
}
