// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(warnings)]

use {
    failure::Error,
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
    pub fn open() -> Result<Wlantap, Error> {
        Ok(Wlantap {
            file: OpenOptions::new()
                .read(true)
                .write(true)
                .open(Path::new("/dev/test/wlantapctl"))?,
        })
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
