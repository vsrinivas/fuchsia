// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use std::path::PathBuf;

/// update out of date crates
/// check that updates did not violate update policies
#[derive(Debug, FromArgs)]
#[argh(subcommand, name = "check")]
pub struct CheckOptions {}

pub fn check_for_new_crates(_manifest_path: PathBuf) -> anyhow::Result<()> {
    println!("WARNING: check for newly added dependencies and get OSRB approval before uploading.");
    Ok(())
}
