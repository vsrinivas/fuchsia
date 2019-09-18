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
    println!("Puppet URLs {:?}", args.puppet_urls);
    if args.puppet_urls.len() == 0 {
        bail!("At least one component URL is required.");
    }
    run_all_puppets(args.puppet_urls, &mut results).await?;
    println!("{}", results.to_json());
    if results.failed() {
        bail!("A test failed")
    } else {
        Ok(())
    }
}

async fn run_all_puppets(urls: Vec<String>, results: &mut results::Results) -> Result<(), Error> {
    for url in urls {
        launch_and_run_puppet(&url, results).await?;
    }
    Ok(())
}

// Coming soon: Quirks (e.g. are duplicate names replaced)
async fn launch_and_run_puppet(
    server_url: &str,
    results: &mut results::Results,
) -> Result<(), Error> {
    let trials = trials::trial_set();
    match puppet::Puppet::connect(server_url).await {
        Ok(mut puppet) => match runner::run(&mut puppet, trials, results).await {
            Err(e) => results.error(format!("Test failed: {}", e)),
            _ => {}
        },
        Err(e) => results.error(format!(
            "Failed to form Puppet - error {:?} - check logs - trying puppet {}.",
            e, server_url
        )),
    }
    Ok(())
}
