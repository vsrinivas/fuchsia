// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::Context,
    anyhow::Error,
    blocking::Unblock,
    fidl_fuchsia_virtualization::{
        GuestMarker, GuestProxy, ManagerMarker, ManagerProxy, RealmMarker, RealmProxy,
    },
    fuchsia_async::{self as fasync, futures::TryFutureExt, futures::TryStreamExt},
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon::{self as zx, HandleBased},
    fuchsia_zircon_status as zx_status,
    std::io::{self, Write},
};

pub struct GuestConsole {
    input: fasync::Socket,
    output: fasync::Socket,
}

impl GuestConsole {
    pub fn new(socket: zx::Socket) -> Result<Self, Error> {
        // We duplicate the handle to enable us to handle r/w simultaneously using streams
        // This is due to a limitation on the fasync::Socket wrapper, not the socket itself
        let guest_console_read =
            fasync::Socket::from_socket(socket.duplicate_handle(zx::Rights::SAME_RIGHTS)?)?;
        let guest_console_write = fasync::Socket::from_socket(socket)?;
        Ok(GuestConsole { input: guest_console_write, output: guest_console_read })
    }

    pub async fn run(&mut self) -> Result<(), zx_status::Status> {
        let console_output = self.output.as_datagram_stream().try_for_each(|message| {
            match io::stdout().write_all(&message) {
                Ok(()) => (),
                Err(err) => return futures::future::ready(Err(From::from(err))),
            }
            futures::future::ready(io::stdout().flush().map_err(From::from))
        });

        futures::future::try_join(
            console_output,
            futures::io::copy(Unblock::new(std::io::stdin()), &mut self.input)
                .err_into::<zx_status::Status>(),
        )
        .await
        .map(|_| ())
        .map_err(From::from)
    }
}

pub fn connect_to_manager() -> Result<ManagerProxy, Error> {
    let manager =
        connect_to_protocol::<ManagerMarker>().context("Failed to connect to manager service")?;
    Ok(manager)
}

pub fn connect_to_env(env_id: u32) -> Result<RealmProxy, Error> {
    let manager = connect_to_manager()?;
    let (realm, realm_server_end) =
        fidl::endpoints::create_proxy::<RealmMarker>().context("Failed to create Realm proxy")?;

    // Connect the realm created to the env specified
    manager
        .connect(env_id, realm_server_end)
        .context("Failed to connect to provided environment")?;

    Ok(realm)
}

#[allow(dead_code)] // TODO(fxbug.dev/89427): Implement guest tool
pub fn connect_to_guest(env_id: u32, cid: u32) -> Result<GuestProxy, Error> {
    let realm = connect_to_env(env_id)?;
    // Connect guest, like realm
    let (guest, guest_server_end) =
        fidl::endpoints::create_proxy::<GuestMarker>().context("Failed to create Guest")?;

    realm
        .connect_to_instance(cid, guest_server_end)
        .context("Could not connect to specified guest instance")?;

    Ok(guest)
}
