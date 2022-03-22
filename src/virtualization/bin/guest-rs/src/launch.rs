// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::arguments,
    crate::services,
    anyhow::{Context, Error},
    fidl_fuchsia_virtualization::{
        GuestConfig, GuestMarker, GuestProxy, ManagerMarker, RealmMarker, RealmProxy,
    },
    fuchsia_async::{self as fasync, futures::TryFutureExt, futures::TryStreamExt},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_url::pkg_url::PkgUrl,
    fuchsia_zircon::sys,
    fuchsia_zircon_status as zx_status,
    std::convert::TryFrom,
    std::io::{self, Write},
};

// Reduce the default maximum memory usage on ARM64, due to the lack of memory
// on the devices we test against.
#[cfg(target_arch = "x86_64")]
const DEFAULT_GUEST_MEM_SIZE: u64 = 3584 << 20;

#[cfg(target_arch = "aarch64")]
const DEFAULT_GUEST_MEM_SIZE: u64 = 1 << 30;

pub fn parse_vmm_args(arguments: &arguments::LaunchArgs) -> GuestConfig {
    // FIDL requires we make a GuestConfig::EMPTY before trying to update fields
    let mut guest_config = GuestConfig::EMPTY;

    if !arguments.cmdline_add.is_empty() {
        guest_config.cmdline_add = Some(arguments.cmdline_add.clone())
    };
    guest_config.default_net = Some(arguments.default_net);
    guest_config.guest_memory = Some(arguments.memory.unwrap_or(DEFAULT_GUEST_MEM_SIZE));
    // There are no assumptions made by this unsafe block; it is only unsafe due to FFI.
    guest_config.cpus = Some(
        arguments.cpus.unwrap_or(unsafe { u8::try_from(sys::zx_system_get_num_cpus()).unwrap() }),
    );
    if !arguments.interrupt.is_empty() {
        guest_config.interrupts = Some(arguments.interrupt.clone())
    };
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
    _realm: RealmProxy,
}

impl GuestLaunch {
    pub async fn new(package_name: String, config: GuestConfig) -> Result<Self, Error> {
        // Take a package, connect to a Manager, create a Guest Realm,
        // launch the package, return guest proxy on success
        // as we need a reference to both the manager and the realm,
        // we can't use services.rs in this instance
        let manager = connect_to_protocol::<ManagerMarker>()
            .context("Failed to connect to manager service")?;
        let (realm, realm_server_end) = fidl::endpoints::create_proxy::<RealmMarker>()
            .context("Failed to create Realm proxy")?;

        manager
            .create(Some(&package_name), realm_server_end)
            .context("Failed to connect Realm to Manager")?;

        // Launch the specified guest
        let url = PkgUrl::new_resource(
            "fuchsia.com".to_string(),
            format!("/{}", package_name),
            None,
            format!("meta/{}.cmx", package_name),
        )?;

        let (guest, guest_server_end) =
            fidl::endpoints::create_proxy::<GuestMarker>().context("Failed to create Guest")?;

        let _guest_cid = realm
            .launch_instance(&url.to_string(), None, config, guest_server_end)
            .map_err(Error::new)
            .await?;

        // We return the realm to prevent it from being dropped
        Ok(GuestLaunch { guest: guest, _realm: realm })
    }
    pub async fn run(&self) -> Result<(), Error> {
        // Set up serial output (grab a zx_socket)
        // Returns a QueryResponseFuture containing ANOTHER future
        let guest_serial_response = self
            .guest
            .get_serial()
            .await?
            .map_err(|status| Error::new(zx_status::Status::from_raw(status)))?;
        let guest_console_response = self
            .guest
            .get_console()
            .await?
            .map_err(|status| Error::new(zx_status::Status::from_raw(status)))?;

        // Turn this stream socket into a a rust stream of data
        let guest_serial_stream =
            fasync::Socket::from_socket(guest_serial_response)?.into_datagram_stream();

        let mut console = services::GuestConsole::new(guest_console_response)?;

        let serial_output = guest_serial_stream.try_for_each(|message| {
            match io::stdout().write_all(&message) {
                Ok(()) => (),
                Err(err) => return futures::future::ready(Err(From::from(err))),
            }
            futures::future::ready(io::stdout().flush().map_err(From::from))
        });

        futures::future::try_join(serial_output, console.run())
            .await
            .map(|_| ())
            .map_err(From::from)
    }
}

#[cfg(test)]
mod test {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl::endpoints::create_proxy_and_stream,
        fuchsia_async::futures::StreamExt,
        fuchsia_zircon::{self as zx},
        futures::future::join,
    };

    // TODO(fxbug.dev/93808): add success tests for launch

    #[fasync::run_until_stalled(test)]
    async fn launch_invalid_serial_returns_error() {
        let (realm_proxy, _realm_stream) = create_proxy_and_stream::<RealmMarker>().unwrap();
        let (guest_proxy, mut guest_stream) = create_proxy_and_stream::<GuestMarker>().unwrap();

        let guest = GuestLaunch { guest: guest_proxy, _realm: realm_proxy };

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
        let (realm_proxy, _realm_stream) = create_proxy_and_stream::<RealmMarker>().unwrap();
        let (guest_proxy, mut guest_stream) = create_proxy_and_stream::<GuestMarker>().unwrap();

        let guest = GuestLaunch { guest: guest_proxy, _realm: realm_proxy };
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
