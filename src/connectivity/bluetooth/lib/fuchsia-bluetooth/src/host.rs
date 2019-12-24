// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::constants::HOST_DEVICE_DIR,
    anyhow::Error,
    fidl_fuchsia_hardware_bluetooth::HostSynchronousProxy,
    fuchsia_zircon as zx,
    std::{
        fs::{read_dir, File},
        path::PathBuf,
    },
};

/// Returns the filesystem paths to the all bt-host devices.
pub fn list_host_devices() -> Vec<PathBuf> {
    let paths = read_dir(HOST_DEVICE_DIR).unwrap();
    paths.filter_map(|entry| entry.ok().and_then(|e| Some(e.path()))).collect::<Vec<PathBuf>>()
}

/// Opens a Host Fidl interface on a bt-host device using a Fidl message
pub fn open_host_channel(device: &File) -> Result<zx::Channel, Error> {
    let dev_channel = fdio::clone_channel(device)?;
    let mut host = HostSynchronousProxy::new(dev_channel);
    let (ours, theirs) = zx::Channel::create()?;
    host.open(theirs)?;
    Ok(ours)
}
