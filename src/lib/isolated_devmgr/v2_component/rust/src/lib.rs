// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_io as fio, fidl_fuchsia_sys2 as fsys, fuchsia_zircon as zx,
    lazy_static::lazy_static,
    std::sync::Mutex,
};

lazy_static! {
    static ref LAUNCHED: Mutex<bool> = Mutex::new(false);
}

/// Launches the isolated driver manager component and binds /dev to the current namespace. This is
/// safe to call multiple times; initialization will only take place once.
pub fn launch_isolated_driver_manager() -> Result<(), Error> {
    let mut launched = LAUNCHED.lock().unwrap();
    if *launched {
        return Ok(());
    }

    // Mark this process as critical so that if this process terminates, all other processes within
    // this job get terminated (e.g. file system processes).
    fuchsia_runtime::job_default()
        .set_critical(zx::JobCriticalOptions::empty(), &*fuchsia_runtime::process_self())?;

    // Connect to the realm to get acccess to the isolated driver manager's outgoing directory.
    let (client, server) = zx::Channel::create()?;
    fuchsia_component::client::connect_channel_to_service::<fsys::RealmMarker>(server)?;
    let mut realm = fsys::RealmSynchronousProxy::new(client);
    let mut child_ref = fsys::ChildRef { name: "isolated_devmgr".to_string(), collection: None };
    let (client, server) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
    realm
        .bind_child(&mut child_ref, server, zx::Time::INFINITE)?
        .map_err(|e| format_err!("Failed to bind to child: {:#?}", e))?;
    let mut exposed_dir =
        fio::DirectorySynchronousProxy::new(client.into_channel().unwrap().into());
    let (client, server) = fidl::endpoints::create_proxy::<fio::NodeMarker>().unwrap();

    // Now open the exported /dev and bind it to our namespace.
    exposed_dir.open(
        fio::OPEN_FLAG_DIRECTORY | fio::OPEN_RIGHT_READABLE,
        fio::MODE_TYPE_DIRECTORY,
        "dev",
        server,
    )?;
    fdio::Namespace::installed()?.bind("/dev", client.into_channel().unwrap().into())?;

    *launched = true;
    Ok(())
}
