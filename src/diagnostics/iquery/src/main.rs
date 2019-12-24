// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        options::{usage, ModeCommand, Options},
        result::IqueryResult,
    },
    anyhow::Error,
    fuchsia_async as fasync,
    std::{env, path::Path},
};

mod commands;
mod formatting;
mod location;
mod options;
mod result;

/// Exceute the command specified in the |options|.
async fn execute(options: &Options) -> Vec<IqueryResult> {
    match options.mode {
        ModeCommand::Cat | ModeCommand::Ls => commands::cat(&options.path).await,
        ModeCommand::Find => commands::find(&options.path, options.recursive).await,
        ModeCommand::Report => {
            // REPORT is a CAT and Options takes care of treating it as such.
            panic!("Unexpected command");
        }
    }
}

/// Print the results to stdout depending on the format and mode specified in |options|.
fn output(results: Vec<IqueryResult>, options: &Options) {
    let output = match options.mode {
        ModeCommand::Cat | ModeCommand::Report => options.formatter().format_recursive(results),
        ModeCommand::Find => options.formatter().format_locations(results),
        ModeCommand::Ls => options.formatter().format_child_listing(results),
    };
    match output {
        Ok(result) => println!("{}", result),
        Err(e) => eprintln!("Error formatting: {}", e),
    };
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let options = Options::read(Box::new(env::args().into_iter())).unwrap_or_else(|error| {
        eprintln!("Failed to parse options: {}", error);
        eprintln!("{}", usage());
        std::process::exit(1);
    });
    if let Some(ref path) = options.global.dir {
        std::env::set_current_dir(Path::new(&path))?;
    }
    let results = execute(&options).await;
    output(results, &options);
    Ok(())
}
