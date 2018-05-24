// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Wlan device objects

#![deny(warnings)]
#![deny(missing_docs)]

#[macro_use]
extern crate fdio;
extern crate fidl_wlan_device as wlan;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;

use std::fmt;
use std::fs::{File, OpenOptions};
use std::path::{Path, PathBuf};

mod sys;

/// Represents a device node.
pub struct Device {
    path: PathBuf,
    node: File,
}

impl Device {
    /// Opens the given path and creates a `Device` for that device node.
    pub fn new<P: AsRef<Path>>(path: P) -> Result<Self, zx::Status> {
        let dev = OpenOptions::new().read(true).write(true).open(&path)?;
        Ok(Self {
            path: PathBuf::from(fdio::device_get_topo_path(&dev)?),
            node: dev,
        })
    }

    /// Returns a reference to the topological path of the device.
    pub fn path(&self) -> &Path {
        &self.path
    }
}

/// Connects to a `Device` that represents a wlan phy.
pub fn connect_wlan_phy(dev: &Device) -> Result<wlan::PhyProxy, zx::Status> {
    let chan = sys::connect_wlanphy_device(&dev.node)?;
    Ok(wlan::PhyProxy::new(async::Channel::from_channel(chan)?))
}

/// Connects to a `Device` that represents a wlan iface.
pub fn connect_wlan_iface(dev: &Device) -> Result<async::Channel, zx::Status> {
    let chan = sys::connect_wlaniface_device(&dev.node)?;
    Ok(async::Channel::from_channel(chan)?)
}

impl fmt::Debug for Device {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        f.debug_struct("Device").field("path", &self.path).finish()
    }
}
