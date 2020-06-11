// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        command_line::CommandLine,
        commands::Command,
        deprecated_options::{usage, DeprecatedOptions, ModeCommand, OptionsReadError},
        result::IqueryResult,
    },
    anyhow::Error,
    argh::FromArgs,
    fuchsia_async as fasync,
    std::{env, path::Path},
};

mod command_line;
mod commands;
mod constants;
mod deprecated_commands;
mod deprecated_options;
mod formatting;
mod location;
mod result;
mod types;

#[cfg(test)]
#[macro_use]
mod tests;

/// Exceute the command specified in the |options|.
async fn legacy_execute(options: &DeprecatedOptions) -> Vec<Result<IqueryResult, Error>> {
    match options.mode {
        ModeCommand::Cat | ModeCommand::Ls => deprecated_commands::cat(&options.path).await,
        ModeCommand::Find => deprecated_commands::find(&options.path, options.recursive).await,
        ModeCommand::Report => {
            // REPORT is a CAT and Options takes care of treating it as such.
            panic!("Unexpected command");
        }
    }
}

/// Print the results to stdout depending on the format and mode specified in |options|.
fn output(results: Vec<IqueryResult>, options: &DeprecatedOptions) {
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

async fn legacy_fallback() -> Result<(), Error> {
    let options = DeprecatedOptions::read(env::args().into_iter()).await.unwrap_or_else(|error| {
        eprintln!("Error: {}", error);
        match error {
            OptionsReadError::ParseError(_) => {
                eprintln!("{}", usage());
            }
            _ => {}
        }
        std::process::exit(1);
    });

    if let Some(ref path) = options.global.dir {
        std::env::set_current_dir(Path::new(&path))?;
    }

    let results = legacy_execute(&options)
        .await
        .into_iter()
        .filter_map(|result| match result {
            Err(e) => {
                eprintln!("Error: {}", e);
                None
            }
            Ok(r) => Some(r),
        })
        .collect::<Vec<_>>();

    if results.is_empty() {
        std::process::exit(1);
    }

    output(results, &options);
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // TODO(fxbug.dev/45458): just use `argh::from_env` once we don't need to support the legacy
    // interface.
    let strings: Vec<String> = std::env::args().collect();
    let strs: Vec<&str> = strings.iter().map(|s| s.as_str()).collect();
    match CommandLine::from_args(&[strs[0]], &strs[1..]) {
        Ok(command_line) => match command_line.execute().await {
            Ok(result) => {
                println!("{}", result);
            }
            Err(err) => eprintln!("{}", err),
        },
        Err(early_exit) => match early_exit.status {
            Ok(()) => {
                println!("{}", early_exit.output);
                std::process::exit(0);
            }
            Err(()) => {
                legacy_fallback().await?;
            }
        },
    }

    Ok(())
}
