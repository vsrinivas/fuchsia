// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![feature(async_await)]

mod puppet;
mod results;
mod runner; // coordinates testing operations
mod trials;

use {
    failure::{bail, Error},
    fidl_test_inspect_validate as validate, fuchsia_async as fasync, fuchsia_syslog as syslog,
    log::*,
};

fn init_syslog() {
    syslog::init_with_tags(&[]).expect("should not fail");
    debug!("Driver did init logger");
}

// TBD whether to launch driver from test or from main.
fn main() {
    init_syslog();
    println!("Hello, world!");
    info!("Hi World!")
}

#[cfg(test)]
mod tests {
    use super::*;

    #[fasync::run_singlethreaded(test)]
    async fn test_main() -> Result<(), Error> {
        crate::init_syslog();
        run_all_puppets().await
    }
}

async fn run_all_puppets() -> Result<(), Error> {
    let mut results = results::Results::new();
    // There will be a puppet for each Inspect library being validated.
    launch_and_run_puppet(
        "fuchsia-pkg://fuchsia.com/inspect_validator_puppet_rust\
         #meta/inspect_validator_puppet_rust.cmx",
        &mut results,
    )
    .await?;
    info!("Result: {:?}", results.to_json());
    println!("{}", results.to_json());
    println!("**DONE**");
    if results.failed() {
        bail!("A test failed")
    } else {
        Ok(())
    }
}

// Coming soon: Quirks (e.g. are duplicate names replaced)
async fn launch_and_run_puppet(
    server_url: &str,
    results: &mut results::Results,
) -> Result<(), Error> {
    info!("URL string {}", server_url);
    let trials = trials::trial_set();
    if let Ok(mut puppet) = puppet::Puppet::connect(server_url, results).await {
        match runner::run(&mut puppet, trials, results) {
            Err(e) => results.error(format!("Test failed: {}", e)),
            _ => {}
        }
    } else {
        results.error(format!("Failed to connect - check logs - trying puppet {}.", server_url));
    }
    Ok(())
}
