// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, ffx_core::ffx_plugin, ffx_update_args::UpdateCommand};

#[ffx_plugin("target_update")]
pub async fn update(_cmd: UpdateCommand) -> Result<(), Error> {
    println!("Hello from the target update plugin.");
    Ok(())
}
