// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::{Command, ListCommand},
        types::Error,
    },
    diagnostics_data::InspectData,
    fuchsia_inspect_contrib::reader::ArchiveReader,
};

/// Returns the selectors for a component whose url contains the `manifest` string.
pub async fn get_selectors_for_manifest(
    manifest: &Option<String>,
    tree_selectors: &Vec<String>,
) -> Result<Vec<String>, Error> {
    match &manifest {
        None => Ok(tree_selectors.clone()),
        Some(manifest) => {
            let list_command = ListCommand { manifest: Some(manifest.clone()), with_url: false };
            let result = list_command
                .execute()
                .await?
                .into_iter()
                .map(|item| item.into_moniker())
                .flat_map(|moniker| {
                    tree_selectors
                        .iter()
                        .map(move |tree_selector| format!("{}:{}", moniker, tree_selector))
                })
                .collect();
            Ok(result)
        }
    }
}

/// Returns the component "moniker" and the hierarchy data for results of
/// reading from the archive using the given selectors.
pub async fn fetch_data(selectors: &[String]) -> Result<Vec<InspectData>, Error> {
    let mut fetcher = ArchiveReader::new().retry_if_empty(false);
    // We support receiving the moniker or a tree selector
    for selector in selectors {
        if selector.contains(":") {
            fetcher = fetcher.add_selector(selector.as_ref());
        } else {
            fetcher = fetcher.add_selector(format!("{}:*", selector));
        }
    }
    let mut results = fetcher.get().await.map_err(|e| Error::Fetch(e))?;
    for result in results.iter_mut() {
        if let Some(hierarchy) = &mut result.payload {
            hierarchy.sort();
        }
    }
    Ok(results)
}
