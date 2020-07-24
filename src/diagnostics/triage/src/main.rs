// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    log::*,
    structopt::StructOpt,
    triage_app_lib::{app::App, Options},
};

/// Reports an [Error] to stdout and logs at "error" level.
fn report_failure(e: Error) {
    error!("Triage failed: {:?}", e);
    println!("Triage failed: {:?}", e);
}

fn run_app() -> Result<i32, Error> {
    let app = App::new(Options::from_args());
    let results = app.run()?;
    results.write_report(&mut std::io::stdout())?;
    Ok(match results.has_warnings() {
        true => 1,
        false => 0,
    })
}

fn main() {
    std::process::exit(match run_app() {
        Ok(code) => code,
        Err(err) => {
            report_failure(err);
            1
        }
    })
}
