// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {blackout_host::Test, failure::Error, std::time::Duration};

fn main() -> Result<(), Error> {
    Test::new("minfs-fsck-target")
        .collect_options()
        .setup_step()
        .load_step(Duration::from_secs(10))
        .reboot_step()
        .verify_step()
        .run()
}
