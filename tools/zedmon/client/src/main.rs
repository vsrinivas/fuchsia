// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod lib;

use anyhow::Error;
use clap::{App, SubCommand};

fn main() -> Result<(), Error> {
    let matches = App::new("zedmon")
        .about("Utility for interacting with Zedmon power measurement device")
        .subcommand(
            SubCommand::with_name("list").about("Lists serial number of connected Zedmon devices"),
        )
        .get_matches();

    if let Some(_) = matches.subcommand_matches("list") {
        let serials = lib::list();
        if serials.is_empty() {
            eprintln!("No Zedmon devices found");
        } else {
            for serial in serials {
                println!("{}", serial);
            }
        }
    }

    Ok(())
}
