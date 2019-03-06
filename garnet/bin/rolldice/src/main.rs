// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use rand::{thread_rng, Rng};
use rolldice_lib::*;
use structopt::StructOpt;

#[derive(StructOpt, Debug)]
#[structopt(name = "rolldice", bin_name = "rolldice", about = "Rolls some number of 6 sided dice.")]
struct Config {
    #[structopt(help = "Number of dice", default_value = "1")]
    number_of_dice: u16,

    #[structopt(short = "r", long = "rowsize", help = "Maximum dice per row", default_value = "8")]
    dice_per_row: u16,
}

fn print_row(rolls: &[RollResult]) {
    // A RollResult can be formatted into a multiline String. Using this primitive:

    // 1. Format all the provided RollResult instances into Strings.
    let formatted: Vec<_> = rolls.iter().map(|roll| format!("{}", roll)).collect();

    // 2. Create iterators for each string that yield lines from the string.
    let iters: Vec<_> = formatted.iter().map(|s| s.lines()).collect();

    // 3. Print each String's first line as a single line, then each String's second line, etc.
    for parts in multizip(iters) {
        let line = parts.as_slice().join(" ");
        println!("{}", line);
    }
}

fn main() {
    let config = Config::from_args();

    if config.dice_per_row == 0 {
        eprintln!("--rowsize must be greater than 0");
        std::process::exit(1);
    }

    // Generate the requested number of die rolls.
    let mut rng = thread_rng();
    let mut rolls = Vec::new();
    for _ in 0..config.number_of_dice {
        rolls.push(rng.gen());
    }

    // Format and display them in rows no larger than dice_per_row.
    for group in rolls.chunks(config.dice_per_row as usize) {
        print_row(group);
    }
}
