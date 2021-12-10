// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, blackout_host::Test, std::time::Duration};

fn main() -> Result<(), Error> {
    Test::new_component(
        "blackout-blobfs-checkerboard-target",
        "blackout-blobfs-checkerboard-target-component",
    )
    .collect_options()
    .setup_step()
    .load_step(Duration::from_secs(30))
    .reboot_step()
    .verify_step(20, Duration::from_secs(5))
    .run()?;
    Ok(())
}
