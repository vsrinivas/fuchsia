// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{commands::types::*, location::all_locations, types::Error},
    argh::FromArgs,
    async_trait::async_trait,
    futures::future::join_all,
    glob,
    lazy_static::lazy_static,
    std::collections::BTreeSet,
};

lazy_static! {
    static ref CURRENT_DIR: Vec<String> = vec![".".to_string()];
}

/// Lists all inspect files (*inspect vmo files, fuchsia.inspect.Tree and
/// fuchsia.inspect.deprecated.Inspect) under the provided paths. If no paths are provided, it'll
/// list under the current directory. At the moment v2 components cannot be seen through the
/// filesystem. Therefore this only outputs v1 files.
#[derive(Default, FromArgs, PartialEq, Debug)]
#[argh(subcommand, name = "list-files")]
pub struct ListFilesCommand {
    #[argh(positional)]
    /// glob paths from where to list files.
    pub paths: Vec<String>,
}

#[async_trait]
impl Command for ListFilesCommand {
    type Result = Vec<String>;

    async fn execute(&self) -> Result<Self::Result, Error> {
        let paths = if self.paths.is_empty() { &*CURRENT_DIR } else { &self.paths };

        let mut result = BTreeSet::new();
        for query_path in paths {
            let location_futs = glob::glob(&query_path)
                .map_err(|e| Error::ParsePath(query_path.clone(), e.into()))?
                .map(|path_result| match path_result {
                    Err(e) => Err(Error::ParsePath(query_path.clone(), e.into())),
                    Ok(path) => Ok(all_locations(path.to_string_lossy().to_string())),
                })
                .collect::<Result<Vec<_>, _>>()?;
            for locations_result in join_all(location_futs).await {
                let locations =
                    locations_result.map_err(|e| Error::ListLocations(query_path.clone(), e))?;
                let paths = locations
                    .into_iter()
                    .map(|location| location.path.to_string_lossy().to_string());
                result.extend(paths);
            }
        }
        Ok(result.into_iter().collect::<Vec<_>>())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::testing,
        fuchsia_async as fasync,
        serde_json::{self, json},
        std::path::Path,
    };

    #[fasync::run_singlethreaded(test)]
    async fn list_files_empty_path_uses_cwd() {
        std::env::set_current_dir(Path::new("/hub")).expect("change dir");
        let (_env, _app) =
            testing::start_basic_component("list-file-test-1").await.expect("create comp 1");
        let command = ListFilesCommand { paths: Vec::new() };
        let result = command.execute().await.expect("got results");
        let expected = json!([
            "./c/iquery_bin_test.cmx/INSTANCE_ID/system_diagnostics/fuchsia.inspect.Tree",
            "./c/observer.cmx/INSTANCE_ID/out/diagnostics/fuchsia.inspect.Tree",
            "./c/observer.cmx/INSTANCE_ID/system_diagnostics/fuchsia.inspect.Tree",
            "./r/list-file-test-1/INSTANCE_ID/c/basic_component.cmx/INSTANCE_ID/out/diagnostics/fuchsia.inspect.Tree",
            "./r/list-file-test-1/INSTANCE_ID/c/basic_component.cmx/INSTANCE_ID/system_diagnostics/fuchsia.inspect.Tree"
        ]);
        testing::assert_result(result, &serde_json::to_string(&expected).unwrap());
    }

    #[fasync::run_singlethreaded(test)]
    async fn list_files() {
        let (_env, _app) =
            testing::start_basic_component("list-file-test-2").await.expect("create comp 1");
        let (_env2, _app2) =
            testing::start_test_component("list-file-test-3").await.expect("create comp 2");
        let command = ListFilesCommand {
            paths: vec![
                "/hub/c/observer.cmx/".to_string(),
                "/hub/r/list-file-test-*/*/c/*/*/out/diagnostics/".to_string(),
            ],
        };
        let result = command.execute().await.expect("got results");
        let expected = json!([
            "/hub/c/observer.cmx/INSTANCE_ID/out/diagnostics/fuchsia.inspect.Tree",
            "/hub/c/observer.cmx/INSTANCE_ID/system_diagnostics/fuchsia.inspect.Tree",
            "/hub/r/list-file-test-2/INSTANCE_ID/c/basic_component.cmx/INSTANCE_ID/out/diagnostics/fuchsia.inspect.Tree",
            "/hub/r/list-file-test-3/INSTANCE_ID/c/test_component.cmx/INSTANCE_ID/out/diagnostics/fuchsia.inspect.Tree",
            "/hub/r/list-file-test-3/INSTANCE_ID/c/test_component.cmx/INSTANCE_ID/out/diagnostics/fuchsia.inspect.deprecated.Inspect",
            "/hub/r/list-file-test-3/INSTANCE_ID/c/test_component.cmx/INSTANCE_ID/out/diagnostics/root.inspect"
        ]);
        testing::assert_result(result, &serde_json::to_string(&expected).unwrap());
    }
}
