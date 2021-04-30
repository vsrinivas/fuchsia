// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod config;

use anyhow::Result;
use config::Config;
use ffx_assembly_args::ImageArgs;
use ffx_core::ffx_bail;

pub fn assemble(args: ImageArgs) -> Result<()> {
    // Read the config.
    let mut config = std::fs::File::open(&args.config)?;
    let config = Config::from_reader(&mut config).or_else(|e| ffx_bail!("Error: {}", e))?;
    println!("Config indicated version: {}", config.version);

    Ok(())
}
