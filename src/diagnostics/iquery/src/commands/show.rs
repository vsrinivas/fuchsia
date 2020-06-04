// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::{types::*, utils},
        formatting::text_formatter,
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
    pub manifest_name: Option<String>,

    #[argh(positional)]
    /// component or tree selectors for which the selectors should be queried. Minimum: 1.
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
        if self.selectors.is_empty() {
            return Err(Error::invalid_arguments("Expected 1 or more selectors. Got zero."));
        }

        // TODO(fxbug.dev/45458): support filtering per manifest name.
        let mut results = utils::fetch_data(&self.selectors)
            .await?
            .into_iter()
            .map(|(moniker, payload)| ShowCommandResultItem { moniker, payload })
            .collect::<Vec<_>>();
        results.sort();

        Ok(results)
    }
}
