// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    structopt::StructOpt,
    triage_lib::{
        act::{self, ActionContext, ActionResults},
        config::{self, OutputFormat, ProgramStateHolder},
        result_format::ActionResultFormatter,
        Options,
    },
};

fn main() {
    if let Err(e) = try_to_run() {
        act::report_failure(e);
    }
}

// TODO(fxb/50449): Use 'argh' crate for parsing arguments.
fn try_to_run() -> Result<(), Error> {
    let ProgramStateHolder { metrics, actions, diagnostic_data, output_format } =
        config::initialize(Options::from_args())?;

    match output_format {
        OutputFormat::Text => {
            let mut action_contexts: Vec<ActionContext<'_>> = diagnostic_data
                .iter()
                .map(|d| act::ActionContext::new(&metrics, &actions, d))
                .collect();

            let action_results: Vec<&ActionResults> =
                action_contexts.iter_mut().map(|c| c.process()).collect();

            let results_formatter = ActionResultFormatter::new(action_results);
            println!("{}", results_formatter.to_warnings());
        }
    };

    Ok(())
}
