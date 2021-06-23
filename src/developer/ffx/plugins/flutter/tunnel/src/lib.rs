// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_core::ffx_plugin, ffx_flutter_tunnel_args::TunnelCommand};

#[ffx_plugin("flutter.tunnel")]
pub async fn tunnel(_cmd: TunnelCommand) -> Result<()> {
    println!("Hello from the flutter tunnel command :)");
    Ok(())
}
