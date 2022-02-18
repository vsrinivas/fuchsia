// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_product_bundle_args::CreateCommand};

pub async fn create_product_bundle(cmd: &CreateCommand) -> Result<()> {
    println!("In pb_create, cmd is: {:#?}", &cmd);
    Ok(())
}
