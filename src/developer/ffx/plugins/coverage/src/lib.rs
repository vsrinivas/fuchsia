// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_core::ffx_plugin, ffx_coverage_args::CoverageCommand};

#[ffx_plugin("coverage")]
pub async fn coverage(_cmd: CoverageCommand) -> Result<()> {
    println!("ffx coverage is WIP");
    Ok(())
}
