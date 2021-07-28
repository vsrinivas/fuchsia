// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {argh::FromArgs, fuchsia_async as fasync};

fn panic() {
    panic!("I HAVE PANICKED");
}

fn async_panic() {
    let mut executor = fasync::SendExecutor::new(4).unwrap();
    let async_code = async {
        panic!("async panic!");
    };
    executor.run(async_code);
}

fn recursive(i_arr: &[u64]) -> u64 {
    println!("recursive([{}, ..])", i_arr[0]);
    let mut arr = [0u64; 512];
    arr[0] = i_arr[0] + 1;
    if arr[0] > 0 {
        return recursive(&arr);
    }
    return arr[0];
}

fn stack_overflow() {
    let arr = [0u64; 1];
    recursive(&arr);
}

#[derive(FromArgs, PartialEq, Debug)]
/// Crash a process in various ways.
struct CommandLineOption {
    #[argh(subcommand)]
    command: Option<Command>,
}

#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand)]
enum Command {
    Panic(Panic),
    AsyncPanic(AsyncPanic),
    StackOverflow(StackOverflow),
}

#[derive(FromArgs, PartialEq, Debug)]
/// call panic!
#[argh(subcommand, name = "panic")]
struct Panic {}

#[derive(FromArgs, PartialEq, Debug)]
/// panic! in async code
#[argh(subcommand, name = "async_panic")]
struct AsyncPanic {}

#[derive(FromArgs, PartialEq, Debug)]
/// overflow the stack by recursion
#[argh(subcommand, name = "stack_ov")]
struct StackOverflow {}

fn main() {
    let option: CommandLineOption = argh::from_env();

    println!("=@ crasher @=");
    match option.command {
        None | Some(Command::Panic(_)) => panic(),
        Some(Command::AsyncPanic(_)) => async_panic(),
        Some(Command::StackOverflow(_)) => stack_overflow(),
    }
    println!("crasher: exiting normally ?!!");
}
