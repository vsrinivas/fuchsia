// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{commands::types::*, location::InspectLocation, result::IqueryResult, types::Error},
    argh::FromArgs,
    async_trait::async_trait,
    derivative::Derivative,
    fuchsia_inspect_node_hierarchy::NodeHierarchy,
    glob::glob,
    serde::Serialize,
    std::str::FromStr,
};

/// Given a path in the hub, prints the inspect contained in it. At the moment this command only
/// works for v1 components as we only have a v1 hub.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "show-file")]
pub struct ShowFileCommand {
    #[argh(positional)]
    /// glob paths to query relative to the current directory. Minimum: 1
    pub paths: Vec<String>,
}

#[derive(Derivative, Serialize, PartialEq)]
#[derivative(Ord, PartialOrd, Eq)]
pub struct ShowFileResultItem {
    path: String,
    #[derivative(Ord = "ignore", PartialOrd = "ignore")]
    pub payload: NodeHierarchy,
}

#[async_trait]
impl Command for ShowFileCommand {
    type Result = Vec<ShowFileResultItem>;

    async fn execute(&self) -> Result<Self::Result, Error> {
        if self.paths.is_empty() {
            return Err(Error::invalid_arguments("Expected 1 or more paths. Got zero."));
        }

        let mut results = Vec::new();

        // TODO(fxbug.dev/45458): refactor to use cleaner methods than IQueryResult and
        // IQueryLocation once the deprecated code is removed. For now sharing a bunch of code.
        for query_path in &self.paths {
            for path_result in
                glob(&query_path).map_err(|e| Error::ParsePath(query_path.clone(), e.into()))?
            {
                let path =
                    path_result.map_err(|e| Error::ParsePath(query_path.clone(), e.into()))?;
                let location = match InspectLocation::from_str(&path.to_string_lossy().to_string())
                {
                    Err(_) => continue,
                    Ok(location) => location,
                };
                match IqueryResult::try_from(location).await {
                    // TODO(fxbug.dev/45458): surface errors too.
                    Err(_) => {}
                    Ok(IqueryResult { location, hierarchy: Some(mut hierarchy) }) => {
                        hierarchy.sort();
                        results.push(ShowFileResultItem {
                            payload: hierarchy,
                            path: location
                                .absolute_path()
                                .unwrap_or(location.path.to_string_lossy().to_string()),
                        });
                    }
                    Ok(_) => {}
                }
            }
        }

        Ok(results)
    }
}

#[cfg(test)]
mod tests {
    use {super::*, crate::testing, fuchsia_async as fasync};

    #[fasync::run_singlethreaded(test)]
    async fn test_no_paths() {
        let command = ShowFileCommand { paths: vec![] };
        assert!(matches!(command.execute().await, Err(Error::InvalidArguments(_))));
    }

    #[fasync::run_singlethreaded(test)]
    async fn show_file() {
        let (_env, _app) =
            testing::start_basic_component("show-file-test-1").await.expect("create comp 1");
        let (_env2, _app2) =
            testing::start_test_component("show-file-test-2").await.expect("create comp 2");
        let command = ShowFileCommand {
            paths: vec![
                "/hub/r/show-file-test-1/*/c/basic_component.cmx/*/out/diagnostics/fuchsia.inspect.Tree".to_string(),
                "/hub/r/show-file-test-2/*/c/test_component.cmx/*/out/diagnostics/*".to_string(),
            ],
        };
        let result = command.execute().await.expect("successful execution");
        testing::assert_result(
            result,
            include_str!("../../test_data/show_file_json_expected.json"),
        );
    }
}
