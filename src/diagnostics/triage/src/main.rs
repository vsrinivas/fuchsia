// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod act; // Perform appropriate actions.
mod config; // Read the config file(s) for metric and action specs.
mod metrics; // Retrieve and calculate the metrics.
mod result_format; // Formats the triage results.
mod validate; // Check config - including that metrics/triggers work correctly.

use {
    act::{ActionContext, ActionResults},
    anyhow::Error,
    config::{OutputFormat, StateHolder},
    result_format::ActionResultFormatter,
    structopt::StructOpt,
};

#[derive(StructOpt, Debug)]
pub struct Options {
    /// Config files
    // TODO(cphoenix): #[argh(option, long = "config")]
    #[structopt(long = "config")]
    config_files: Vec<String>,

    /// inspect.json file
    // TODO(cphoenix): #[argh(option, long = "inspect")]
    #[structopt(long)]
    inspect: Option<String>,

    /// How to print the results.
    #[structopt(long = "output_format")]
    output_format: OutputFormat,

    /// Directories to read "inspect.json" files from.
    #[structopt(long = "directory")]
    directories: Vec<String>,
}

fn main() {
    if let Err(e) = try_to_run() {
        act::report_failure(e);
    }
}

fn try_to_run() -> Result<(), Error> {
    let StateHolder { metrics, actions, inspect_contexts, output_format } =
        config::initialize(Options::from_args())?;
    // TODO(cphoenix): argh::from_env();

    let mut action_contexts: Vec<ActionContext> =
        inspect_contexts.iter().map(|c| act::ActionContext::new(&metrics, &actions, c)).collect();

    let action_results: Vec<&ActionResults> =
        action_contexts.iter_mut().map(|c| c.process()).collect();

    let mut action_labels = Vec::new();
    for result in &action_results {
        action_labels.append(&mut result.get_actions());
    }
    action_labels.sort();
    action_labels.dedup();

    let results_formatter = ActionResultFormatter::new(action_results, action_labels);
    match output_format {
        OutputFormat::Text => println!("{}", results_formatter.to_warnings()),
        OutputFormat::CSV => println!("{}", results_formatter.to_csv().to_string(",")),
    };

    Ok(())
}
