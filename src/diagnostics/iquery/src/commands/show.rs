// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::{types::*, utils},
        text_formatter,
        types::{Error, ToText},
    },
    argh::FromArgs,
    async_trait::async_trait,
    derivative::Derivative,
    fuchsia_inspect_node_hierarchy::NodeHierarchy,
    serde::Serialize,
    std::cmp::Ordering,
};

/// Prints the inspect hierarchies that match the given selectors.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "show")]
pub struct ShowCommand {
    #[argh(option)]
    /// the name of the manifest file that we are interested in. If this is provided, the output
    /// will only contain monikers for components whose url contains the provided name.
    pub manifest: Option<String>,

    #[argh(positional)]
    /// selectors for which the selectors should be queried. If no selectors are provided, inspect
    /// data for the whole system will be returned. If `--manifest` is provided then the selectors
    /// should be tree selectors, otherwise component selectors or full selectors.
    pub selectors: Vec<String>,
}

#[derive(Derivative, Serialize, PartialEq)]
#[derivative(Ord, Eq)]
pub struct ShowCommandResultItem {
    pub moniker: String,
    #[derivative(Ord = "ignore")]
    pub payload: NodeHierarchy,
}

impl PartialOrd for ShowCommandResultItem {
    fn partial_cmp(&self, other: &Self) -> Option<Ordering> {
        Some(self.moniker.cmp(&other.moniker))
    }
}

impl ToText for Vec<ShowCommandResultItem> {
    fn to_text(self) -> String {
        self.into_iter()
            .map(|item| text_formatter::format(&item.moniker, item.payload))
            .collect::<Vec<_>>()
            .join("\n")
    }
}

#[async_trait]
impl Command for ShowCommand {
    type Result = Vec<ShowCommandResultItem>;

    async fn execute(&self) -> Result<Self::Result, Error> {
        let selectors = utils::get_selectors_for_manifest(&self.manifest, &self.selectors).await?;
        let mut results = utils::fetch_data(&selectors)
            .await?
            .into_iter()
            .map(|(moniker, payload)| ShowCommandResultItem { moniker, payload })
            .collect::<Vec<_>>();
        results.sort();

        Ok(results)
    }
}
