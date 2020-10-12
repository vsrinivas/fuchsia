// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod lib;
mod protocol;

use {
    anyhow::Error,
    clap::{App, Arg, ArgMatches, SubCommand},
    std::{
        fs::File,
        io::{Read, Write},
    },
};

/// Raises a stop signal for Zedmon recording upon the first input to stdin (which, given stdin
/// buffering, means on the first press of ENTER.)
struct StdinStopper {
    stdin: termion::AsyncReader,
    buffer: [u8; 1],
    stopped: bool,
}

impl StdinStopper {
    fn new() -> StdinStopper {
        StdinStopper { stdin: termion::async_stdin(), buffer: [0], stopped: false }
    }
}

impl lib::StopSignal for StdinStopper {
    fn should_stop(&mut self) -> Result<bool, Error> {
        Ok(self.stopped || self.stdin.read(&mut self.buffer)? > 0)
    }
}

fn main() -> Result<(), Error> {
    let matches = App::new("zedmon")
        .about("Utility for interacting with Zedmon power measurement device")
        .subcommand(
            SubCommand::with_name("list").about("Lists serial number of connected Zedmon devices"),
        )
        .subcommand(
            SubCommand::with_name("record").about("Record power data").arg(
                Arg::with_name("out")
                    .help("Name of output file. Use '-' for stdout.")
                    .short("o")
                    .long("out")
                    .takes_value(true),
            ),
        )
        .get_matches();

    match matches.subcommand() {
        ("list", _) => run_list(),
        ("record", Some(arg_matches)) => run_record(arg_matches)?,
        _ => panic!("Invalid subcommand"),
    };

    Ok(())
}

/// Runs the "list" subcommand.
fn run_list() {
    let serials = lib::list();
    if serials.is_empty() {
        eprintln!("No Zedmon devices found");
    } else {
        for serial in serials {
            println!("{}", serial);
        }
    }
}

/// Runs the "record" subcommand".
fn run_record(arg_matches: &ArgMatches<'_>) -> Result<(), Error> {
    let (output, dest_name): (Box<dyn Write + Send>, &str) = match arg_matches.value_of("out") {
        None => (Box::new(File::create("zedmon.csv")?), "zedmon.csv"),
        Some("-") => (Box::new(std::io::stdout()), "stdout"),
        Some(filename) => (Box::new(File::create(filename)?), filename),
    };
    let dest_name = dest_name.to_string();

    let zedmon = lib::zedmon();

    // TODO(fxbug.dev/61471): Consider incorporating the time offset directly into report
    // timestamps.
    let (offset, uncertainty) = zedmon.get_time_offset_nanos()?;
    println!("Time offset: {}ns ± {}ns\n", offset, uncertainty);

    println!("Recording to {}. Press ENTER to stop.", dest_name);
    zedmon.read_reports(output, StdinStopper::new())
}
