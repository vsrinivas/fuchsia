// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Wlan device objects

#![deny(warnings)]
#![deny(missing_docs)]

extern crate failure;
#[macro_use]
extern crate fdio;
extern crate fidl_wlan_device as wlan;
extern crate fuchsia_async as async;
extern crate fuchsia_zircon as zx;

use failure::Error;
use std::fmt;
use std::fs::{File, OpenOptions};
use std::path::{Path, PathBuf};

mod sys;

/// Represents a WLAN Phy device node.
pub struct WlanPhy {
    dev_path: PathBuf,
    dev_node: File,
}

impl WlanPhy {
    /// Opens the given path and creates a `WlanPhy` for that device node.
    pub fn new<P: AsRef<Path>>(path: P) -> Result<Self, Error> {
        let dev = OpenOptions::new().read(true).write(true).open(&path)?;
        Ok(WlanPhy {
            dev_path: PathBuf::from(fdio::device_get_topo_path(&dev)?),
            dev_node: dev,
        })
    }

    /// Returns a reference to the topological path of the device.
    pub fn path(&self) -> &Path {
        &self.dev_path
    }

    /// Retrieves a zircon channel to the WLAN Phy device, for use with the WLAN Phy fidl service.
    pub fn connect(&self) -> Result<wlan::PhyProxy, zx::Status> {
        let chan = sys::connect_wlanphy_device(&self.dev_node).map_err(|_| zx::Status::INTERNAL)?;
        Ok(wlan::PhyProxy::new(async::Channel::from_channel(chan)?))
    }
}

impl fmt::Debug for WlanPhy {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        f.debug_struct("WlanPhy")
            .field("path", &self.dev_path)
            .finish()
    }
}
