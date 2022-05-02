// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, blackout_host::TestEnv, fuchsia_async as fasync, std::time::Duration};

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let mut test = TestEnv::new_component("minfs-fsck-target", "minfs_fsck_target").await;
    test.setup_step()
        .load_step(Duration::from_secs(10))
        .reboot_step()
        .verify_step(10, Duration::from_secs(1));
    test.run()?;
    Ok(())
}
