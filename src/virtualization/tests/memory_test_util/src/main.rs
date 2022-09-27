// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![deny(warnings)]

use {
    argh::FromArgs,
    std::{thread, time},
};

#[derive(FromArgs, PartialEq, Debug)]
/// Top-level command.
pub struct Commands {
    #[argh(subcommand)]
    nested: SubCommands,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum SubCommands {
    Alloc(AllocArgs),
}

#[derive(FromArgs, PartialEq, Debug)]
/// Allocate memory and write test pattern into it
#[argh(subcommand, name = "alloc")]
struct AllocArgs {
    #[argh(option, default = "10")]
    /// allocation size in megabytes
    size_mb: usize,

    #[argh(option, default = "1")]
    /// number of memory allocations
    num: usize,

    #[argh(option, default = "0")]
    /// how long to wait before freeing memory in seconds
    wait_before_free_sec: usize,

    #[argh(switch)]
    /// input is required to exit.
    press_enter_to_continue: bool,
}

fn run_alloc_command(args: AllocArgs) {
    let mut mem = Vec::new();
    for _ in 0..args.num {
        mem.push(vec![1u8; args.size_mb * 1024 * 1024]);
    }
    println!("Allocated {} MiB", args.size_mb * args.num);

    if args.wait_before_free_sec > 0 {
        thread::sleep(time::Duration::from_secs(args.wait_before_free_sec as u64));
    }

    if args.press_enter_to_continue {
        println!("Press ENTER to drop allocated memory and exit the test...");
        let mut _input = String::new();
        std::io::stdin().read_line(&mut _input).unwrap();
    }
    drop(mem);
    println!("Dropped {} MiB", args.size_mb * args.num);
}

fn main() -> std::io::Result<()> {
    let cmd = argh::from_env::<Commands>();
    match cmd.nested {
        SubCommands::Alloc(alloc_args) => run_alloc_command(alloc_args),
    }

    Ok(())
}
