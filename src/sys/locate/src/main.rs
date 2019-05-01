// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await, await_macro)]

use failure::{err_msg, format_err, Error, ResultExt};
use fidl_fuchsia_sys_index::{ComponentIndexMarker, FuzzySearchError};
use fuchsia_async as fasync;
use fuchsia_component::client::{launch, launcher};
use std::convert::{TryFrom, TryInto};
use std::env;

#[fasync::run_singlethreaded]
async fn main() {
    match await!(run_locate()) {
        Err(e) => {
            eprintln!("Error: {}", e);
            std::process::exit(1);
        }
        Ok(()) => {}
    }
}

async fn run_locate() -> Result<(), Error> {
    let cfg: LocateConfig = env::args().try_into()?;

    let launcher = launcher().context("Failed to open launcher service.")?;
    let app = launch(
        &launcher,
        "fuchsia-pkg://fuchsia.com/component_index#meta/component_index.cmx".to_string(),
        None,
    )
    .context("Failed to launch component_index service.")?;

    let component_index = app
        .connect_to_service::<ComponentIndexMarker>()
        .context("Failed to connect to component_index service.")?;

    match await!(component_index.fuzzy_search(&cfg.search_keyword))? {
        Ok(res_vec) => fuzzy_search_result(res_vec, cfg),
        Err(e) => fuzzy_search_error(e, cfg),
    }
}

fn fuzzy_search_result(res_vec: Vec<String>, cfg: LocateConfig) -> Result<(), Error> {
    if res_vec.is_empty() {
        return Err(format_err!("\"{}\" did not match any components.", cfg.search_keyword));
    }
    if cfg.list {
        for res in &res_vec {
            println!("{}", res);
        }
    } else {
        if res_vec.len() == 1 {
            println!("{}", res_vec[0]);
        } else {
            for res in &res_vec {
                eprintln!("{}", res);
            }
            return Err(format_err!(
                "\"{}\" matched more than one component. Try `locate --list` instead.",
                cfg.search_keyword
            ));
        }
    }
    Ok(())
}

fn fuzzy_search_error(e: FuzzySearchError, cfg: LocateConfig) -> Result<(), Error> {
    if e == FuzzySearchError::MalformedInput {
        return Err(format_err!(
            "\"{}\" contains unsupported characters. Valid characters are [A-Z a-z 0-9 / _ - .].",
            cfg.search_keyword
        ));
    }
    Err(err_msg("fuchsia.sys.index.FuzzySearch could not serve the input query."))
}

struct LocateConfig {
    list: bool,
    search_keyword: String,
}

impl TryFrom<env::Args> for LocateConfig {
    type Error = Error;
    fn try_from(args: env::Args) -> Result<LocateConfig, Error> {
        let mut args = args.peekable();
        // Ignore arg[0]
        let _ = args.next();

        // Consume arg[1] if it is --list
        let list = args.peek() == Some(&"--list".to_string());
        if list {
            let _ = args.next();
        }

        // Ensure nothing beyond current arg.
        if let (Some(search_keyword), None) = (args.next(), args.next()) {
            return Ok(LocateConfig { list, search_keyword });
        } else {
            help();
            return Err(err_msg("Unable to parse args."));
        }
    }
}

fn help() {
    println!(
        r"Usage: locate [--list] <search_keyword>

Locates the fuchsia-pkg URL of <search_keyword>.

Options:
  --list    Allows matching of more than one component."
    )
}
