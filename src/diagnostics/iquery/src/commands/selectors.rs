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
    fuchsia_inspect_node_hierarchy::NodeHierarchy,
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
    pub manifest_name: Option<String>,

    #[argh(positional)]
    /// component or tree selectors for which the selectors should be queried. Minimum: 1.
    pub selectors: Vec<String>,
}

#[async_trait]
impl Command for SelectorsCommand {
    type Result = Vec<String>;

    async fn execute(&self) -> Result<Self::Result, Error> {
        if self.selectors.is_empty() {
            return Err(Error::invalid_arguments("Expected 1 or more selectors. Got zero."));
        }

        // TODO(fxbug.dev/45458): support filtering per manifest name.

        let mut result = utils::fetch_data(&self.selectors)
            .await?
            .into_iter()
            .flat_map(|(component_selector, hierarchy)| {
                get_selectors(component_selector, hierarchy)
            })
            .collect::<Vec<_>>();
        result.sort();
        Ok(result)
    }
}

fn get_selectors(component_selector: String, hierarchy: NodeHierarchy) -> Vec<String> {
    hierarchy
        .property_iter()
        .map(|(node_path, maybe_property)| {
            let node_selector = node_path
                .iter()
                .map(|s| selectors::sanitize_string_for_selectors(s))
                .collect::<Vec<String>>()
                .join("/");
            let mut result = format!("{}:{}", component_selector, node_selector);
            if let Some(property) = maybe_property {
                let property_selector = selectors::sanitize_string_for_selectors(property.name());
                result = format!("{}:{}", result, property_selector)
            }
            result
        })
        .collect()
}

#[cfg(test)]
mod tests {
    use {super::*, crate::testing, fuchsia_async as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn test_no_selectors() {
        let command = SelectorsCommand { manifest_name: None, selectors: vec![] };
        assert!(matches!(command.execute().await, Err(Error::InvalidArguments(_))));
    }

    #[fasync::run_singlethreaded(test)]
    async fn selectors_json() {
        let (_env, _app) =
            testing::start_basic_component("selectors-test").await.expect("create comp 1");
        let (_env2, _app2) =
            testing::start_basic_component("selectors-test2").await.expect("create comp 2");
        let (_env3, _app3) =
            testing::start_test_component("selectors-test3").await.expect("create comp 3");
        let mut result;
        loop {
            let command = SelectorsCommand {
                manifest_name: None,
                selectors: vec![
                    "selectors-test/basic_component.cmx:root/fuchsia.inspect.Health".to_string(),
                    "selectors-test2/basic_component.cmx:root".to_string(),
                    "selectors-test3/test_component.cmx".to_string(),
                ],
            };
            result = command.execute().await.expect("successful execution");
            // We might get less than 123 in the first try. Keep retrying otherwise we'll flake.
            // Usually only another retry is needed if all the data wasn't present on the first
            // attempt.
            if result.len() == 123 {
                break;
            }
        }
        testing::assert_result(
            result,
            include_str!("../../test_data/selectors_json_expected.json"),
        );
    }
}
