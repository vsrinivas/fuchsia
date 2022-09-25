// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::{types::*, utils},
        types::Error,
    },
    argh::FromArgs,
    async_trait::async_trait,
    diagnostics_data::{Inspect, InspectData},
    diagnostics_hierarchy::DiagnosticsHierarchy,
    selectors,
};

/// Lists all available full selectors (component selector + tree selector).
/// If a selector is provided, it’ll only print selectors for that component.
/// If a full selector (component + tree) is provided, it lists all selectors under the given node.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "selectors")]
pub struct SelectorsCommand {
    #[argh(option)]
    /// the name of the manifest file that we are interested in. If this is provided, the output
    /// will only contain monikers for components whose url contains the provided name.
    pub manifest: Option<String>,

    #[argh(positional)]
    /// selectors for which the selectors should be queried. Minimum: 1 unless `--manifest` is set.
    /// When `--manifest` is provided then the selectors should be tree selectors, otherwise
    /// they can be component selectors or full selectors.
    pub selectors: Vec<String>,

    #[argh(option)]
    /// the path from where to get the ArchiveAccessor connection. If the given path is a
    /// directory, the command will look for a `fuchsia.diagnostics.ArchiveAccessor` service file.
    /// If the given path is a service file, the command will attempt to connect to it as an
    /// ArchiveAccessor.
    pub accessor_path: Option<String>,
}

#[async_trait]
impl Command for SelectorsCommand {
    type Result = Vec<String>;

    async fn execute<P: DiagnosticsProvider>(&self, provider: &P) -> Result<Self::Result, Error> {
        if self.selectors.is_empty() && self.manifest.is_none() {
            return Err(Error::invalid_arguments("Expected 1 or more selectors. Got zero."));
        }
        let selectors = utils::get_selectors_for_manifest(
            &self.manifest,
            &self.selectors,
            &self.accessor_path,
            provider,
        )
        .await?;
        let selectors = utils::expand_selectors(selectors)?;
        let mut results = provider.snapshot::<Inspect>(&self.accessor_path, &selectors).await?;
        for result in results.iter_mut() {
            if let Some(hierarchy) = &mut result.payload {
                hierarchy.sort();
            }
        }
        Ok(inspect_to_selectors(results))
    }
}

fn get_selectors(component_selector: String, hierarchy: DiagnosticsHierarchy) -> Vec<String> {
    hierarchy
        .property_iter()
        .flat_map(|(node_path, maybe_property)| maybe_property.map(|prop| (node_path, prop)))
        .map(|(node_path, property)| {
            let node_selector = node_path
                .iter()
                .map(|s| selectors::sanitize_string_for_selectors(s))
                .collect::<Vec<String>>()
                .join("/");
            let property_selector = selectors::sanitize_string_for_selectors(property.name());
            format!("{}:{}:{}", component_selector, node_selector, property_selector)
        })
        .collect()
}

fn inspect_to_selectors(inspect_data: Vec<InspectData>) -> Vec<String> {
    let mut result = inspect_data
        .into_iter()
        .filter_map(|schema| {
            let moniker = schema.moniker;
            schema.payload.map(|hierarchy| get_selectors(moniker, hierarchy))
        })
        .flat_map(|results| results)
        .collect::<Vec<_>>();
    result.sort();
    result
}
