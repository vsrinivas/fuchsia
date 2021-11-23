// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{bail, Context, Result},
    clap::{App, Arg},
    std::fs,
};

mod lib;

use lib::{verify_component_resolvers, AllowList, ScrutinyQueryComponentResolvers};

pub struct VerifyComponentResolvers {
    stamp_path: String,
    allowlist_path: String,
}

impl VerifyComponentResolvers {
    /// Creates a new VerifyComponentResolvers instance with a `stamp_path` that is written
    /// to if the verification succeeds and a `allowlist_path` which lists the
    /// scheme/moniker/protocol tuples to check and the allowed matching components.
    fn new<S: Into<String>>(stamp_path: S, allowlist_path: S) -> Self {
        Self { stamp_path: stamp_path.into(), allowlist_path: allowlist_path.into() }
    }

    /// Launches Scrutiny and performs the component resolver analysis. The
    /// results are then filtered based on the provided allowlist and any
    /// errors that are not allowlisted cause the verification to fail listing
    /// all non-allowlisted errors.
    fn verify(&self) -> Result<()> {
        let scrutiny = ScrutinyQueryComponentResolvers::from_env()?;

        let allowlist: AllowList = serde_json5::from_str(
            &fs::read_to_string(&self.allowlist_path).context("Failed to read allowlist")?,
        )
        .context("Failed to deserialize allowlist")?;

        if let Some(violations) = verify_component_resolvers(scrutiny, allowlist)? {
            bail!(
                "
Static Component Resolver Capability Analysis Error:
The component resolver verifier found some components configured to be resolved using
a privileged component resolver.

If it is intended for these components to be resolved using the given resolver, add an entry
to the allowlist located at: {}

Verification Errors:
{}",
                self.allowlist_path,
                serde_json::to_string_pretty(&violations).unwrap()
            );
        }

        fs::write(&self.stamp_path, "Verified\n").context("failed to write stamp file")?;
        Ok(())
    }
}

/// A small shim interface around the Scrutiny framework that takes the
/// `stamp` and `allowlist` paths from the build.
fn main() -> Result<()> {
    simplelog::SimpleLogger::init(simplelog::LevelFilter::Error, simplelog::Config::default())?;
    let args = App::new("scrutiny_verify_component_resolvers")
        .version("1.0")
        .author("Fuchsia Authors")
        .about("Verifies component framework v2 component resolvers")
        .arg(
            Arg::with_name("stamp")
                .long("stamp")
                .required(true)
                .help("The stamp file output location")
                .value_name("stamp")
                .takes_value(true),
        )
        .arg(
            Arg::with_name("allowlist")
                .long("allowlist")
                .required(true)
                .help("The allowlist file input location")
                .value_name("allowlist")
                .takes_value(true),
        )
        .get_matches();

    let verify_component_resolvers = VerifyComponentResolvers::new(
        args.value_of("stamp").unwrap(),
        args.value_of("allowlist").unwrap(),
    );
    verify_component_resolvers.verify()
}
