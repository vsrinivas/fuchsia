// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::types::*,
        location::{InspectLocation, InspectObject},
        text_formatter,
        types::{Error, ToText},
    },
    argh::FromArgs,
    async_trait::async_trait,
    derivative::Derivative,
    diagnostics_hierarchy::DiagnosticsHierarchy,
    serde::Serialize,
    std::str::FromStr,
};

/// Given a path, prints the inspect contained in it. At the moment this command only
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
    pub payload: DiagnosticsHierarchy,
}

impl ToText for Vec<ShowFileResultItem> {
    fn to_text(self) -> String {
        self.into_iter()
            .map(|item| text_formatter::format(&item.path, item.payload))
            .collect::<Vec<_>>()
            .join("\n")
    }
}

#[async_trait]
impl Command for ShowFileCommand {
    type Result = Vec<ShowFileResultItem>;

    async fn execute(&self) -> Result<Self::Result, Error> {
        if self.paths.is_empty() {
            return Err(Error::invalid_arguments("Expected 1 or more paths. Got zero."));
        }

        let mut results = Vec::new();

        for path in expand_paths(&self.paths)? {
            let location = match InspectLocation::from_str(&path) {
                Err(_) => continue,
                Ok(location) => location,
            };

            let path = location.path.to_string_lossy().to_string();

            match location.load().await {
                Err(e) => {
                    return Err(Error::ReadLocation(path.to_string(), e));
                }
                Ok(InspectObject { location, hierarchy: Some(mut hierarchy) }) => {
                    hierarchy.sort();
                    results.push(ShowFileResultItem {
                        payload: hierarchy,
                        path: location.absolute_path().unwrap_or(path),
                    });
                }
                Ok(_) => {}
            }
        }

        Ok(results)
    }
}

pub fn expand_paths(query_paths: &[String]) -> Result<Vec<String>, Error> {
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
