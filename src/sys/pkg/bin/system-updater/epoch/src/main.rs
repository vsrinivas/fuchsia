// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    epoch::EpochFile,
    std::fs::{read_to_string, File},
};

mod args;
mod history;

use {args::Args, history::History};

fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();

    let history: History = read_to_string(args.history())
        .with_context(|| format!("while reading {:?} to string", args.history()))?
        .parse()
        .context("while parsing history")?;

    let epoch: EpochFile = history.into();
    let output_file = File::create(args.output())
        .with_context(|| format!("while creating {:?}", args.output()))?;
    let () = serde_json::to_writer(output_file, &epoch).context("while writing data")?;

    Ok(())
}
