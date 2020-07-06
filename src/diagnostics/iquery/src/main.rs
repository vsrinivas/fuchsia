// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{command_line::CommandLine, commands::Command},
    anyhow::Error,
    argh, fuchsia_async as fasync,
};

mod command_line;
mod commands;
mod constants;
mod location;
mod text_formatter;
mod types;

#[cfg(test)]
#[macro_use]
mod tests;

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let command_line: CommandLine = argh::from_env();
    match command_line.execute().await {
        Ok(result) => {
            println!("{}", result);
        }
        Err(err) => {
            eprintln!("{}", err);
            std::process::exit(1);
        }
    }
    Ok(())
}
