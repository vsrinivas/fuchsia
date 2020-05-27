// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{commands::types::*, types::Error},
    argh::FromArgs,
    async_trait::async_trait,
    derivative::Derivative,
    fuchsia_inspect::testing::InspectDataFetcher,
    fuchsia_inspect_node_hierarchy::{
        serialization::{HierarchyDeserializer, RawJsonNodeHierarchySerializer},
        NodeHierarchy,
    },
    serde::Serialize,
    std::cmp::Ordering,
};

/// Prints the inspect hierarchies that match the given selectors. If none are given, it prints
/// everything.
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

#[async_trait]
impl Command for ShowCommand {
    type Result = Vec<ShowCommandResultItem>;

    async fn execute(&self) -> Result<Self::Result, Error> {
        if self.selectors.is_empty() {
            return Err(Error::invalid_arguments("Expected 1 or more selectors. Got zero."));
        }

        // TODO(fxbug.dev/45458): support filtering per manifest name.
        // TODO(fxbug.dev/45458): support printing in TEXT.

        let mut fetcher = InspectDataFetcher::new();
        // We support receiving the moniker or a tree selector
        for selector in &self.selectors {
            if selector.contains(":") {
                fetcher = fetcher.add_selector(selector.as_ref());
            } else {
                fetcher = fetcher.add_selector(format!("{}:root", selector));
            }
        }
        let mut results = fetcher.get_raw_json().await.map_err(|e| Error::Fetch(e))?;

        // TODO(fxbug.dev/52641): we need to convert the payload to a NodeHierarchy to be able to
        // sort it. it would be better request the Archivist to do this itself.
        let mut results = results
            .as_array_mut()
            .ok_or(Error::ArchiveInvalidJson)?
            .into_iter()
            .map(|result| {
                let payload =
                    result.get_mut("payload").ok_or(Error::archive_missing_property("payload"))?;
                let mut hierarchy: NodeHierarchy<String> =
                    RawJsonNodeHierarchySerializer::deserialize(payload.take())
                        .map_err(|_| Error::ArchiveInvalidJson)?;
                hierarchy.sort();
                let moniker = result
                    .get("moniker")
                    .ok_or(Error::archive_missing_property("moniker"))?
                    .as_str()
                    .ok_or(Error::ArchiveInvalidJson)?
                    .to_string();
                Ok(ShowCommandResultItem { moniker, payload: hierarchy })
            })
            .collect::<Result<Vec<_>, Error>>()?;

        results.sort();

        Ok(results)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::testing, fuchsia_async as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn test_no_selectors() {
        let command = ShowCommand { manifest_name: None, selectors: vec![] };
        matches!(command.execute().await, Err(Error::InvalidArguments(_)));
    }

    #[fasync::run_singlethreaded(test)]
    async fn show_json() {
        let (_env, _app) = testing::start_basic_component("test").await.expect("create comp 1");
        let (_env2, _app2) = testing::start_basic_component("test2").await.expect("create comp 2");
        let (_env3, _app3) = testing::start_basic_component("test3").await.expect("create comp 3");
        let mut result;
        loop {
            let command = ShowCommand {
                manifest_name: None,
                selectors: vec![
                    "test/basic_component.cmx:root/fuchsia.inspect.Health".to_string(),
                    "test2/basic_component.cmx:root:iquery".to_string(),
                    "test3/basic_component.cmx".to_string(),
                ],
            };
            result = command.execute().await.expect("successful execution");
            // We might get less than 3 in the first try. Keep retrying otherwise we'll flake.
            // Usually only another retry is needed if all the data wasn't present on the first
            // attempt.
            if result.len() == 3 {
                break;
            }
        }
        testing::assert_result(result, include_str!("../../test_data/show_json_expected.json"));
    }
}
