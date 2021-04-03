// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_assembly_args::AssemblyCommand, ffx_core::ffx_plugin};

#[ffx_plugin("assembly_enabled")]
pub async fn assembly(cmd: AssemblyCommand) -> Result<()> {
    println!("Hello from the assembly plugin :)");
    println!("  value: {}", cmd.value);
    Ok(())
}
