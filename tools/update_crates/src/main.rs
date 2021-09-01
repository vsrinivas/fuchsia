// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use argh::FromArgs;
use std::path::PathBuf;

mod check;
mod update;

/// update a Cargo.toml based on output from cargo-outdated and an overrides file.
#[derive(Debug, FromArgs)]
struct Options {
    /// path to the 3p crates' Cargo.toml
    #[argh(option)]
    manifest_path: PathBuf,

    /// path to the outdated.toml override file
    #[argh(option)]
    overrides: PathBuf,

    #[argh(subcommand)]
    mode: Mode,
}

#[derive(Debug, FromArgs)]
#[argh(subcommand)]
enum Mode {
    Update(update::UpdateOptions),
    Check(check::CheckOptions),
}

fn main() -> anyhow::Result<()> {
    let Options { overrides, manifest_path, mode } = argh::from_env();

    match mode {
        Mode::Update(opts) => update::update_crates(overrides, manifest_path, opts),
        Mode::Check(_) => check::check_for_new_crates(manifest_path),
    }
}
