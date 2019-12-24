// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Wlan device objects

#![deny(missing_docs)]

use {
    anyhow, fidl_fuchsia_wlan_device as wlan, fidl_fuchsia_wlan_mlme as mlme, fuchsia_zircon as zx,
    std::{
        fmt,
        fs::{File, OpenOptions},
        path::{Path, PathBuf},
    },
};

pub use isolated_devmgr::IsolatedDeviceEnv;

mod isolated_devmgr;
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
        Ok(Self { path: PathBuf::from(fdio::device_get_topo_path(&dev)?), node: dev })
    }

    /// Returns a reference to the topological path of the device.
    pub fn path(&self) -> &Path {
        &self.path
    }
}

/// Connects to a `Device` that represents a wlan phy.
pub fn connect_wlan_phy(dev: &Device) -> Result<wlan::PhyProxy, anyhow::Error> {
    sys::connect_wlanphy_device(&dev.node)
}

/// Connects to a `Device` that represents a wlan iface.
pub fn connect_wlan_iface(dev: &Device) -> Result<mlme::MlmeProxy, anyhow::Error> {
    sys::connect_wlaniface_device(&dev.node)
}

impl fmt::Debug for Device {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> Result<(), fmt::Error> {
        f.debug_struct("Device").field("path", &self.path).finish()
    }
}

/// Encapsulate phy and iface devices as well as the environment where they exist
pub trait DeviceEnv {
    /// Path to the directory where new phy deivces will be spawned
    const PHY_PATH: &'static str = "/dev/class/wlanphy";
    /// (soon to be depreacated) Path to the directory where new iface devies will be spawned
    const IFACE_PATH: &'static str = "/dev/class/wlanif";

    /// Opens a directory for device watcher to detect new devices
    fn open_dir<P: AsRef<Path>>(path: P) -> Result<File, zx::Status> {
        File::open(path).map_err(|e| e.into())
    }

    /// Creates a Device (defined above) from a file at the given path
    fn device_from_path(path: &PathBuf) -> Result<Device, zx::Status> {
        Device::new(path)
    }
}

/// The real environment is the global Fuchsia environment
pub struct RealDeviceEnv;
impl DeviceEnv for RealDeviceEnv {}
