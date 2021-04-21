// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_core::ffx_plugin, ffx_repository_serve_args::ServeCommand};

#[ffx_plugin()]
pub async fn serve(_cmd: ServeCommand) -> Result<()> {
    Ok(())
}
