// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Result, ffx_assembly_args::*, ffx_core::ffx_plugin};

mod base_package;
mod blobfs;
mod config;
mod extra_hash_descriptor;
mod fvm;
mod operations;
mod update_package;
mod util;
mod zbi;

pub mod vbmeta;
pub mod vfs;

#[ffx_plugin("assembly_enabled")]
pub async fn assembly(cmd: AssemblyCommand) -> Result<()> {
    // Dispatch to the correct operation based on the command.
    match cmd.op_class {
        OperationClass::Image(args) => operations::image::assemble(args),
        OperationClass::Extract(args) => operations::extract::extract(args),
    }
}
