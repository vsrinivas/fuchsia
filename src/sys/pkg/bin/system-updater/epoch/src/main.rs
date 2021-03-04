// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    std::fs::read_to_string,
};

mod args;
mod history;

use {args::Args, history::History};

fn main() -> Result<(), Error> {
    let args: Args = argh::from_env();

    let _history: History = read_to_string(args.history())
        .with_context(|| format!("while reading {:?} to string", args.history()))?
        .parse()
        .context("while parsing history")?;

    Ok(())
}
