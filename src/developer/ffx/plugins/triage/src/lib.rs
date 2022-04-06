// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_core::ffx_plugin, ffx_triage_args::TriageCommand};

#[ffx_plugin("triage.enabled")]
pub async fn triage(_cmd: TriageCommand) -> Result<()> {
    println!("Experimental triage plugin");
    Ok(())
}
