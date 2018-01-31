// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Wlan device objects

#![deny(warnings)]
#![deny(missing_docs)]

extern crate failure;
#[macro_use]
extern crate fdio;
extern crate fidl;
extern crate fuchsia_zircon as zircon;
extern crate garnet_lib_wlan_fidl as wlan;

use std::fmt;
use std::fs::{File, OpenOptions};
use std::io;
use std::path::{Path, PathBuf};

mod sys;

/// Represents a WLAN Phy device node.
pub struct WlanPhy {
    dev_path: PathBuf,
    dev_node: File,
}

impl WlanPhy {
    /// Opens the given path and creates a `WlanPhy` for that device node.
    pub fn new<P: AsRef<Path>>(path: P) -> io::Result<Self> {
        let dev = OpenOptions::new().read(true).write(true).open(&path)?;
        let mut path_buf = PathBuf::new();
        path_buf.push(path);
        Ok(WlanPhy { dev_path: path_buf, dev_node: dev })
    }

    /// Queries the WLAN Phy device for its capabilities.
    pub fn query(&self) -> Result<wlan::WlanInfo, zircon::Status> {
        sys::query_wlanphy_device(&self.dev_node).map_err(|_| zircon::Status::INTERNAL)
    }

    /// Creates a new WLAN Iface with the given role.
    pub fn create_iface(&self, role: wlan::MacRole) -> Result<wlan::WlanIface, zircon::Status> {
        sys::create_wlaniface(&self.dev_node, role).map_err(|_| zircon::Status::INTERNAL)
    }

    /// Destroys the WLAN Iface with the given id.
    pub fn destroy_iface(&self, id: u16) -> Result<(), zircon::Status> {
        sys::destroy_wlaniface(&self.dev_node, id).map_err(|_| zircon::Status::INTERNAL)
    }
}

impl fmt::Debug for WlanPhy {
    fn fmt(&self, f: &mut fmt::Formatter) -> Result<(), fmt::Error> {
        f.debug_struct("WlanPhy").field("path", &self.dev_path).finish()
    }
}
