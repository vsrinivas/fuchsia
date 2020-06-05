// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::path::Path;

const ETHERNET_VDEV_PATH: &'static str = "/vdev/class/ethernet";

fn main() -> Result<(), anyhow::Error> {
    // Make sure the ethernet device class directory exists even when we have no
    // endpoints.
    if !Path::new(ETHERNET_VDEV_PATH).is_dir() {
        return Err(anyhow::anyhow!("Directory {} does not exist", ETHERNET_VDEV_PATH));
    }

    Ok(())
}
