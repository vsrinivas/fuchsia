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
    derivative::Derivative,
    fuchsia_inspect_node_hierarchy::NodeHierarchy,
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
        let mut results = utils::fetch_data(&self.selectors)
            .await?
            .into_iter()
            .map(|(moniker, payload)| ShowCommandResultItem { moniker, payload })
            .collect::<Vec<_>>();
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
        assert!(matches!(command.execute().await, Err(Error::InvalidArguments(_))));
    }

    #[fasync::run_singlethreaded(test)]
    async fn show_json() {
        let (_env, _app) =
            testing::start_basic_component("show-test").await.expect("create comp 1");
        let (_env2, _app2) =
            testing::start_basic_component("show-test2").await.expect("create comp 2");
        let (_env3, _app3) =
            testing::start_basic_component("show-test3").await.expect("create comp 3");
        let mut result;
        loop {
            let command = ShowCommand {
                manifest_name: None,
                selectors: vec![
                    "show-test/basic_component.cmx:root/fuchsia.inspect.Health".to_string(),
                    "show-test2/basic_component.cmx:root:iquery".to_string(),
                    "show-test3/basic_component.cmx".to_string(),
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
