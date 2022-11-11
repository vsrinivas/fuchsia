// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::{ffx_bail, ffx_error},
    fuchsia_url::AbsoluteComponentUrl,
};

pub mod query;
pub mod rcs;

/// Verifies that `url` can be parsed as a fuchsia-pkg CM URL
/// Returns the name of the component manifest, if the parsing was successful.
pub fn verify_fuchsia_pkg_cm_url(url: &str) -> Result<String> {
    let url = match AbsoluteComponentUrl::parse(url) {
        Ok(url) => url,
        Err(e) => ffx_bail!("URL parsing error: {:?}", e),
    };

    let manifest = url
        .resource()
        .split('/')
        .last()
        .ok_or(ffx_error!("Could not extract manifest filename from URL"))?;

    if let Some(name) = manifest.strip_suffix(".cm") {
        Ok(name.to_string())
    } else if manifest.ends_with(".cmx") {
        ffx_bail!(
            "{} is a legacy component manifest. Run it using `ffx component run-legacy`",
            manifest
        )
    } else {
        ffx_bail!(
            "{} is not a component manifest! Component manifests must end in the `cm` extension.",
            manifest
        )
    }
}
