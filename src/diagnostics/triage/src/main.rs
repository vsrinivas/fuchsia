// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    log::*,
    structopt::StructOpt,
    triage_lib::{app::App, Options},
};

/// Reports an [Error] to stdout and logs at "error" level.
pub fn report_failure(e: Error) {
    error!("Triage failed: {:?}", e);
    println!("Triage failed: {:?}", e);
}

fn main() {
    let app = App::new(Options::from_args());
    match app.run() {
        Ok(results) => results.write_report(&mut std::io::stdout()).unwrap(),
        Err(e) => report_failure(e),
    };
}
