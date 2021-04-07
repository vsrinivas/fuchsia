// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_core::ffx_plugin, ffx_starnix_shell_args::ShellStarnixCommand};

#[ffx_plugin("starnix_enabled")]
pub async fn shell_starnix(_shell: ShellStarnixCommand) -> Result<()> {
    Ok(())
}
