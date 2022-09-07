// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, ffx_core::ffx_command, ffx_gpu_sub_command::SubCommand};

#[ffx_command()]
#[derive(FromArgs, Debug, PartialEq)]
#[argh(subcommand, name = "gpu", description = "Access GPU usage information")]
/// Top-level command for "ffx profile gpu".
pub struct GpuCommand {
    #[argh(subcommand)]
    pub subcommand: SubCommand,
}
