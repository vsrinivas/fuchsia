// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Result},
    argh::from_env,
    fuchsia_url::pkg_url::PkgUrl,
    security_pkg_test_util::load_config,
    std::fs::{read, write},
};

/// Flags for rewrite_packages_json_domain.
#[derive(argh::FromArgs, Debug, PartialEq)]
pub struct Args {
    /// absolute path to input `packages.json` file to have its domain name
    /// modified.
    #[argh(option)]
    pub input: String,

    /// absolute path to output `packages.json` file that will contain
    /// substituted domain names.
    #[argh(option)]
    pub output: String,

    /// absolute path to the test configuration file that designates an
    /// `update_domain` to be used in domain name substitution.
    #[argh(option)]
    pub test_config: String,

    /// the domain name that appears in `--input` that is to be substituted.
    #[argh(option)]
    pub in_domain: String,
}

fn main() -> Result<()> {
    let _args @ Args { input, output, test_config, in_domain } = &from_env();
    let out_domain = load_config(test_config).update_domain;
    let input_packages_json_contents = read(input).context("failed to read input packages.json")?;
    let input_packages_json =
        update_package::parse_packages_json(input_packages_json_contents.as_slice())
            .context("failed parse packages.json")?;
    let output_packages_json = input_packages_json
        .into_iter()
        .map(|pkg_url| match pkg_url.host() == in_domain {
            true => pkg_url
                .replace_host(out_domain.clone())
                .context("failed to subsitute host part of package URL"),
            false => Ok(pkg_url.clone()),
        })
        .collect::<Result<Vec<PkgUrl>>>()?;
    let packages_json_contents = update_package::serialize_packages_json(&output_packages_json)
        .context("failed to serialize transformed packages.json")?;
    write(output, packages_json_contents).context("failed to write transformed packages.json")?;
    Ok(())
}
