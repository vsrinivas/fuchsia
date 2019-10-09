// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod data; // Inspect data maintainer/updater/scanner-from-vmo; compare engine
mod metrics; // Evaluates memory performance of Inspect library
mod puppet; // Interface to target Inspect library wrapper programs (puppets)
mod results; // Stores and formats reports-to-user
mod runner; // Coordinates testing operations
mod trials; // Defines the trials to run

use {
    argh::FromArgs,
    failure::{bail, Error},
    fidl_test_inspect_validate as validate, fuchsia_async as fasync, fuchsia_syslog as syslog,
    log::*,
    std::str::FromStr,
};

fn init_syslog() {
    syslog::init_with_tags(&[]).expect("should not fail");
    debug!("Driver did init logger");
}

/// Validate Inspect VMO formats written by 'puppet' programs controlled by
/// this Validator program and exercising Inspect library implementations.
//#[derive(StructOpt, Debug)]
#[derive(Debug, FromArgs)]
struct Opt {
    /// report results in a pretty human-readable format. Without this flag,
    /// results will be printed as JSON.
    #[argh(option, long = "output", default = "OutputType::Json")]
    output: OutputType,

    /// required arg(s): The URL(s) of the puppet(s).
    #[argh(option, long = "url")]
    puppet_urls: Vec<String>,

    /// quiet has no effect.
    #[argh(switch, long = "quiet")]
    quiet: bool,

    #[argh(switch, long = "verbose")]
    /// verbose has no effect.
    verbose: bool,

    #[argh(switch, long = "version", short = 'v')]
    /// version prints the version information and exits.
    version: bool,
}

#[derive(Debug)]
enum OutputType {
    Text,
    Json,
}

impl FromStr for OutputType {
    type Err = Error;
    fn from_str(s: &str) -> Result<OutputType, Error> {
        Ok(match s {
            "text" => OutputType::Text,
            "json" => OutputType::Json,
            _ => bail!("Output type must be 'text' or 'json'"),
        })
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    init_syslog();
    let mut results = results::Results::new();
    let Opt { output, puppet_urls, version, .. } = argh::from_env();
    if version {
        println!("Inspect Validator version 0.8. See README.md for more information.");
        return Ok(());
    }
    run_all_puppets(puppet_urls, &mut results).await;
    match output {
        OutputType::Text => results.print_pretty_text(),
        OutputType::Json => println!("{}", results.to_json()),
    }
    if results.failed() {
        bail!("A test failed")
    } else {
        Ok(())
    }
}

async fn run_all_puppets(urls: Vec<String>, results: &mut results::Results) {
    if urls.len() == 0 {
        results.error("At least one component URL is required.".to_string());
    }
    for url in urls {
        runner::run_all_trials(&url, results).await;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fasync::run_singlethreaded(test)]
    async fn url_is_required() {
        let mut results = results::Results::new();
        run_all_puppets(vec![], &mut results).await;
        assert!(results.failed());
        assert!(results.to_json().contains("At least one"));
    }

    #[fasync::run_singlethreaded(test)]
    async fn bad_url_fails() {
        let mut results = results::Results::new();
        run_all_puppets(vec!["a".to_owned()], &mut results).await;
        assert!(results.failed());
        assert!(results.to_json().contains("URL may be invalid"), results.to_json());
    }

    #[fasync::run_singlethreaded(test)]
    async fn all_urls_are_tried() {
        let mut results = results::Results::new();
        run_all_puppets(vec!["a".to_owned(), "b".to_owned()], &mut results).await;
        assert!(results.to_json().contains("invalid: a"));
        assert!(results.to_json().contains("invalid: b"));
    }

    // The only way to test success is to actually start a component, and that's
    // not suitable for unit tests. Failure on a valid URL will be caught in integration
    // tests.
}
