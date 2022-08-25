// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_audio_gen_args::GenCommand, ffx_core::ffx_plugin};

#[ffx_plugin("audio")]
pub async fn gen(_cmd: GenCommand) -> Result<()> {
    println!("todo: audio gen subcommand.");
    Ok(())
}
