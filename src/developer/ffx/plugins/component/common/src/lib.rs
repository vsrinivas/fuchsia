// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    component_debug::lifecycle::LifecycleError,
    errors::{ffx_bail, ffx_error},
    fuchsia_url::AbsoluteComponentUrl,
    moniker::{AbsoluteMoniker, AbsoluteMonikerBase},
};

pub mod query;
pub mod rcs;

static LIFECYCLE_ERROR_HELP: &'static str =
    "To learn more, see https://fuchsia.dev/go/components/run-errors";

/// Parses a string into an absolute component URL.
pub fn parse_component_url(url: &str) -> Result<AbsoluteComponentUrl> {
    let url = match AbsoluteComponentUrl::parse(url) {
        Ok(url) => url,
        Err(e) => ffx_bail!("URL parsing error: {:?}", e),
    };

    let manifest = url
        .resource()
        .split('/')
        .last()
        .ok_or(ffx_error!("Could not extract manifest filename from URL"))?;

    if let Some(_) = manifest.strip_suffix(".cm") {
        Ok(url)
    } else {
        ffx_bail!(
            "{} is not a component manifest! Component manifests must end in the `cm` extension.",
            manifest
        )
    }
}

/// Format a LifecycleError into an error message that is suitable for `ffx component`.
pub fn format_lifecycle_error(err: LifecycleError) -> errors::FfxError {
    match err {
        LifecycleError::ExpectedDynamicInstance { moniker } => {
            let moniker = AbsoluteMoniker::root().descendant(&moniker);
            ffx_error!("\nError: {} does not reference an instance in a collection.\nTo learn more about collections, see https://fuchsia.dev/go/components/collections\n", moniker)
        },
        LifecycleError::InstanceAlreadyExists { moniker } => {
            let moniker = AbsoluteMoniker::root().descendant(&moniker);
            ffx_error!("\nError: {} already exists.\nUse `ffx component show` to get information about the instance.\n{}\n", moniker, LIFECYCLE_ERROR_HELP)
        },
        LifecycleError::InstanceNotFound { moniker } => {
            let moniker = AbsoluteMoniker::root().descendant(&moniker);
            ffx_error!("\nError: {} does not exist.\nUse `ffx component list` or `ffx component show` to find the correct instance.\n{}\n", moniker, LIFECYCLE_ERROR_HELP)
        },
        LifecycleError::Internal(e) => ffx_error!("\nError: Internal error in LifecycleController ({:?}).\nCheck target logs (`ffx log`) for error details printed by component_manager.\n{}\n", e, LIFECYCLE_ERROR_HELP),
        LifecycleError::Fidl(e) => ffx_error!("\nError: FIDL error communicating with LifecycleController ({:?}).\nCheck target logs (`ffx log`) for error details printed by component_manager.\n{}\n", e, LIFECYCLE_ERROR_HELP),
    }
}
