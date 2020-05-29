// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{commands::types::*, location::InspectLocation, result::IqueryResult, types::Error},
    argh::FromArgs,
    async_trait::async_trait,
    derivative::Derivative,
    fuchsia_inspect_node_hierarchy::NodeHierarchy,
    serde::Serialize,
    std::str::FromStr,
};

/// Given a path in the hub, prints the inspect contained in it. At the moment this command only
/// works for v1 components as we only have a v1 hub.
#[derive(FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "show-file")]
pub struct ShowFileCommand {
    #[argh(positional)]
    /// paths to query relative to the current directory. Minimum: 1
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
        for path in expand_paths(&self.paths)? {
            let location = match InspectLocation::from_str(&path) {
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

        Ok(results)
    }
}

fn expand_paths(query_paths: &[String]) -> Result<Vec<String>, Error> {
    let mut result = Vec::new();
    for query_path in query_paths {
        if query_path.ends_with(".inspect") {
            if query_path.contains("*") {
                println!(concat!(
                    "WARNING: you might no results when pointing directly to a *.inspect ",
                    "vmo file. This is due to http://fxbug.dev/40888. As a workaround use `*` ",
                    "instead of `root.inspect`."
                ));
            } else {
                // Just push this query path. For example, if the command was called with
                // /hub/c/kcounter_inspect.cmx/\*/out/diagnostics/\* and the shell matched a single
                // file then at least we'll get it here. Otherwise due to http://fxbug.dev/40888,
                // glob will return an empty list for vmo files.
                // TODO(fxbug.dev/40888): remove this
                result.push(query_path.clone());
                continue;
            }
        }
        let path_results = glob::glob(&query_path)
            .map_err(|e| Error::ParsePath(query_path.clone(), e.into()))?
            .collect::<Vec<_>>();
        for path_result in path_results {
            let path = path_result.map_err(|e| Error::ParsePath(query_path.clone(), e.into()))?;
            result.push(path.to_string_lossy().to_string());
        }
    }
    Ok(result)
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

    #[fasync::run_singlethreaded(test)]
    async fn inspect_vmo_file_directly() {
        let (_env, _app) =
            testing::start_test_component("show-file-vmo-2").await.expect("create comp 2");
        let paths = expand_paths(&[
            "/hub/r/show-file-vmo-2/*/c/test_component.cmx/*/out/diagnostics/*".to_string(),
        ])
        .expect("got paths");

        // Pass only the path to the vmo file. Without the workaround in `get_paths` comments this
        // wouldn't work and the `result` would be an emtpy list.
        let path = paths
            .into_iter()
            .find(|p| p.ends_with("root.inspect"))
            .expect("found root.inspect path");
        let command = ShowFileCommand { paths: vec![path] };
        let result = command.execute().await.expect("successful execution");
        testing::assert_result(result, include_str!("../../test_data/show_file_vmo_expected.json"));
    }
}
