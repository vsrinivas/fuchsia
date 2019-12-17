// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod act; // Perform appropriate actions.
mod config; // Read the config file(s) for metric and action specs.
mod metrics; // Retrieve and calculate the metrics.
mod validate; // Check config - including that metrics/triggers work correctly.

use {failure::Error, structopt::StructOpt};

#[derive(StructOpt, Debug)]
pub struct Options {
    /// Config files
    // TODO(cphoenix): #[argh(option, long = "config")]
    #[structopt(long = "config")]
    config_files: Vec<String>,

    /// inspect.json file
    // TODO(cphoenix): #[argh(option, long = "inspect")]
    #[structopt(long, default_value = "~/inspect.json")]
    inspect: String,
}

fn main() {
    if let Err(e) = try_to_run() {
        act::report_failure(e);
    }
}

fn try_to_run() -> Result<(), Error> {
    let state = config::initialize(Options::from_args())?; // TODO(cphoenix): argh::from_env();
    let mut context = act::ActionContext::new(&state.metrics, &state.actions, &state.inspect_data);
    context.process();
    context.print_warnings();
    Ok(())
}
