// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::Context,
    anyhow::Error,
    fidl_fuchsia_virtualization::{
        GuestMarker, GuestProxy, ManagerMarker, ManagerProxy, RealmMarker, RealmProxy,
    },
    fuchsia_component::client::connect_to_protocol,
};

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
