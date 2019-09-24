// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod data;
mod puppet;
mod results;
mod runner; // coordinates testing operations
mod trials;

use {
    failure::{bail, Error},
    fidl_test_inspect_validate as validate, fuchsia_async as fasync, fuchsia_syslog as syslog,
    log::*,
    structopt::StructOpt,
};

fn init_syslog() {
    syslog::init_with_tags(&[]).expect("should not fail");
    debug!("Driver did init logger");
}

#[derive(StructOpt, Debug)]
/// Validate Inspect VMO formats written by 'puppet' programs controlled by
/// this Validator program and exercising Inspect library implementations.
struct Opt {
    /// Required (positional) arg(s): The URL(s) of the puppet(s).
    #[structopt()]
    puppet_urls: Vec<String>,
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    init_syslog();
    let mut results = results::Results::new();
    let args = Opt::from_args();
    run_all_puppets(args.puppet_urls, &mut results).await;
    println!("{}", results.to_json());
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
        runner::run_all_trials(&url, results).await.ok();
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
        assert!(results.to_json().contains("URL may be invalid"));
    }

    #[fasync::run_singlethreaded(test)]
    async fn all_are_tried() {
        let mut results = results::Results::new();
        run_all_puppets(vec!["a".to_owned(), "b".to_owned()], &mut results).await;
        assert!(results.to_json().contains("trying puppet a"));
        assert!(results.to_json().contains("trying puppet b"));
    }

    // The only way to test success is to actually start a component, and that's
    // not suitable for unit tests. Failure on a valid URL will be caught in integration
    // tests.
}
