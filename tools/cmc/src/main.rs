// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]
// This crate doesn't comply with all 2018 idioms
#![allow(elided_lifetimes_in_paths)]

//! `cmc` is the Component Manifest Compiler.

pub use cml::{self, error, one_or_many, translate};
use error::Error;
use std::fs;
use std::path::PathBuf;
use std::process;
use structopt::StructOpt;

mod compile;
mod format;
mod include;
mod merge;
mod opts;
mod util;
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
        opts::Commands::Merge { files, output, fromfile } => merge::merge(files, output, fromfile)?,
        opts::Commands::Include { file, output, depfile, includepath } => {
            include::merge_includes(file, output, depfile, includepath)?
        }
        opts::Commands::CheckIncludes { file, expected_includes, fromfile } => {
            include::check_includes(file, expected_includes, fromfile)?
        }
        opts::Commands::Format { file, pretty, cml, inplace, mut output } => {
            if inplace {
                output = Some(file.clone());
            }
            format::format(&file, pretty, cml, output)?;
        }
        opts::Commands::Compile { file, output, depfile, includepath } => {
            // FIXME: Don't just unwrap it.
            compile::compile(&file, &output.unwrap(), depfile, includepath)?
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
