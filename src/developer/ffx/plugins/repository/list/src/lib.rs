// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_core::ffx_plugin, ffx_repository_list_args::ListCommand};

#[ffx_plugin()]
pub async fn list(_cmd: ListCommand) -> Result<()> {
    Ok(())
}
