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

    async fn execute<P: DiagnosticsProvider>(&self, _provider: &P) -> Result<Self::Result, Error> {
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
                let paths = locations.into_iter().map(|location| {
                    let path = location.path.to_string_lossy().to_string();
                    if path.starts_with("./") || path.starts_with("/") {
                        path
                    } else {
                        format!("./{}", path)
                    }
                });
                result.extend(paths);
            }
        }
        Ok(result.into_iter().collect::<Vec<_>>())
    }
}
