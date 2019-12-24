// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints,
    fidl_fuchsia_wlan_tap as wlantap, fuchsia_zircon as zx,
    std::{
        fs::{File, OpenOptions},
        path::Path,
    },
};

pub struct Wlantap {
    file: File,
}

impl Wlantap {
    pub fn open() -> Result<Self, Error> {
        const PATH_STR: &str = "/dev/test/wlantapctl";
        Ok(Self { file: OpenOptions::new().read(true).write(true).open(Path::new(PATH_STR))? })
    }

    pub fn open_from_isolated_devmgr() -> Result<Self, Error> {
        const PATH_STR: &str = "test/wlantapctl";
        Ok(Self { file: wlan_dev::IsolatedDeviceEnv::open_file(PATH_STR)? })
    }

    pub fn create_phy(
        &self,
        mut config: wlantap::WlantapPhyConfig,
    ) -> Result<wlantap::WlantapPhyProxy, Error> {
        let (ours, theirs) = endpoints::create_proxy()?;

        let channel = fdio::clone_channel(&self.file)?;
        let mut wlantap_ctl_proxy = wlantap::WlantapCtlSynchronousProxy::new(channel);

        let status = wlantap_ctl_proxy.create_phy(&mut config, theirs, zx::Time::INFINITE)?;

        let () = zx::ok(status)?;
        Ok(ours)
    }
}
