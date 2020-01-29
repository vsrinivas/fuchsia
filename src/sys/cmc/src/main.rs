// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

//! `cmc` is the Component Manifest Compiler.

use cm_json::Error;
use std::fs;
use std::path::PathBuf;
use std::process;
use structopt::StructOpt;

mod cml;
mod compile;
mod format;
mod merge;
mod one_or_many;
mod opts;
mod validate;

fn main() {
    if let Err(msg) = run_cmc() {
        eprintln!("{}", msg);
        process::exit(1);
    }
}

fn run_cmc() -> Result<(), Error> {
    let opt = opts::Opt::from_args();
    match opt.cmd {
        opts::Commands::Validate { files, extra_schemas } => {
            validate::validate(&files, &extra_schemas)?
        }
        opts::Commands::Merge { files, output } => merge::merge(files, output)?,
        opts::Commands::Format { file, pretty, output } => format::format(&file, pretty, output)?,
        opts::Commands::Compile { file, pretty, output } => {
            compile::compile(&file, pretty, output)?
        }
    }
    if let Some(stamp_path) = opt.stamp {
        stamp(stamp_path)?;
    }
    Ok(())
}

fn stamp(stamp_path: PathBuf) -> Result<(), Error> {
    fs::File::create(stamp_path)?;
    Ok(())
}
