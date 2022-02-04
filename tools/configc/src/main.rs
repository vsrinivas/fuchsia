// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod client_cpp_elf;
mod client_fidl;
mod client_rust;
mod cvf;

use anyhow::Error;
use argh::FromArgs;
use client_cpp_elf::GenerateCppElfSource;
use client_fidl::GenerateFidlSource;
use client_rust::GenerateRustSource;
use cvf::GenerateValueFile;

#[derive(FromArgs, PartialEq, Debug)]
/// Tool for compiling structured configuration artifacts
struct Command {
    #[argh(subcommand)]
    sub: Subcommand,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum Subcommand {
    GenerateValueFile(GenerateValueFile),
    GenerateFidlSource(GenerateFidlSource),
    GenerateRustSource(GenerateRustSource),
    GenerateCppElfSource(GenerateCppElfSource),
}

fn main() -> Result<(), Error> {
    let command = argh::from_env::<Command>();

    match command.sub {
        Subcommand::GenerateValueFile(cmd) => cmd.generate(),
        Subcommand::GenerateFidlSource(cmd) => cmd.generate(),
        Subcommand::GenerateRustSource(cmd) => cmd.generate(),
        Subcommand::GenerateCppElfSource(cmd) => cmd.generate(),
    }
}
