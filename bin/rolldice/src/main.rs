// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[macro_use] extern crate structopt;
extern crate rand;

use rand::{thread_rng, Rng, Rand};
use std::fmt;
use structopt::StructOpt;

const corner : char = '+';
const horiz  : char = '-';
const vert   : char = '|';
const pip    : char = '*';
const blank  : char = ' ';

enum RollResult {
    One,
    Two,
    Three,
    Four,
    Five,
    Six,
}

impl Rand for RollResult {
    fn rand<R: Rng>(rng: &mut R) -> RollResult {
        match rng.gen_range(0, 6) {
            0 => RollResult::One,
            1 => RollResult::Two,
            2 => RollResult::Three,
            3 => RollResult::Four,
            4 => RollResult::Five,
            _ => RollResult::Six,
        }
    }
}

impl fmt::Display for RollResult {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        let pips = match self {
            RollResult::One => [
                [blank, blank, blank],
                [blank, pip,   blank],
                [blank, blank, blank],
            ],
            RollResult::Two => [
                [blank, blank, pip],
                [blank, blank, blank],
                [pip,   blank, blank],
            ],
            RollResult::Three => [
                [blank, blank, pip],
                [blank, pip,   blank],
                [pip,   blank, blank],
            ],
            RollResult::Four => [
                [pip,   blank, pip],
                [blank, blank, blank],
                [pip,   blank, pip],
            ],
            RollResult::Five => [
                [pip,   blank, pip],
                [blank, pip,   blank],
                [pip,   blank, pip],
            ],
            RollResult::Six => [
                [pip,   blank, pip],
                [pip,   blank, pip],
                [pip,   blank, pip],
            ],
        };

        writeln!(f, "{}{}{}{}{}", corner, horiz, horiz, horiz, corner);
        for row in &pips {
            write!(f, "{}", vert);
            for c in row {
                write!(f, "{}", c);
            }
            writeln!(f, "{}", vert);
        }
        writeln!(f, "{}{}{}{}{}", corner, horiz, horiz, horiz, corner);

        Ok(())
    }
}

#[derive(StructOpt, Debug)]
#[structopt(name="rolldice", bin_name="rolldice", about="Rolls some number of 6 sided dice.")]
struct Config {
    #[structopt(help="Number of dice", default_value="1")]
    number_of_dice: u16
}

fn main() {
    let config = Config::from_args();

    let mut rng = thread_rng();
    for i in 0..config.number_of_dice {
        let roll : RollResult = rng.gen();
        println!("{}", roll);
    }
}
