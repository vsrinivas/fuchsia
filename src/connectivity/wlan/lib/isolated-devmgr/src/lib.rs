// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Utility functions for opening files and directories from within an isolated devmgr that is
//! created for unittests and integration tests.

use {
    fidl_fuchsia_wlan_devmgr::IsolatedDevmgrMarker,
    fuchsia_component::client::connect_to_protocol,
    fuchsia_zircon as zx,
    std::{fs::File, path::Path},
    wlan_dev::{Device, DeviceEnv},
};

/// An environment (usually in a test) where a isolated device manager is exposed.
pub struct IsolatedDeviceEnv;
impl DeviceEnv for IsolatedDeviceEnv {
    const PHY_PATH: &'static str = "class/wlanphy";

    fn open_dir<P: AsRef<Path>>(path: P) -> Result<File, zx::Status> {
        IsolatedDeviceEnv::open_dir(path)
    }

    fn device_from_path<P: AsRef<Path>>(path: P) -> Result<Device, zx::Status> {
        let dev = IsolatedDeviceEnv::open_file(path)?;
        Device::new(dev)
    }
}

impl IsolatedDeviceEnv {
    fn open(path: &str, flags: u32) -> Result<File, zx::Status> {
        let isolated_devmgr =
            connect_to_protocol::<IsolatedDevmgrMarker>().expect("connecting to isolated devmgr.");
        let (node_proxy, server_end) =
            fidl::endpoints::create_endpoints().expect("creating channel for devfs node");
        isolated_devmgr
            .open(flags, 0, path, server_end)
            .expect("opening devfs node in isolated devmgr");
        fdio::create_fd(node_proxy.into_channel().into())
    }

    /// Opens a path as a directory
    pub fn open_dir<P: AsRef<Path>>(path: P) -> Result<File, zx::Status> {
        let flags = fidl_fuchsia_io::OPEN_FLAG_DIRECTORY | fidl_fuchsia_io::OPEN_RIGHT_READABLE;
        Self::open(path.as_ref().to_str().unwrap(), flags)
    }

    /// Opens a path as a file
    pub fn open_file<P: AsRef<Path>>(path: P) -> Result<File, zx::Status> {
        let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE;
        Self::open(path.as_ref().to_str().unwrap(), flags)
    }
}
