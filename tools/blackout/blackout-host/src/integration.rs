// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Specific declarations and convenience methods for running blackout in CI.
//!
//! There are some specific things we need to do only in the infra test environment, such as
//! setting up our own bootserver for netbooting.

/// Run an infra-specific bootserver using the environment variables we expect when run by
/// botanist.
pub fn run_bootserver() -> anyhow::Result<std::process::Child> {
    let bootserver_cmd = std::env::var("BOOTSERVER_PATH")?;
    let image_manifest_path = std::env::var("IMAGE_MANIFEST_PATH")?;
    let node_name = std::env::var("FUCHSIA_NODENAME")?;
    std::process::Command::new(bootserver_cmd)
        .arg("-n")
        .arg(node_name)
        .arg("-images")
        .arg(image_manifest_path)
        .arg("-mode")
        .arg("netboot")
        .spawn()
        .map_err(Into::into)
}
