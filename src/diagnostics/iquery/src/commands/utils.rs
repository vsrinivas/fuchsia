// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        commands::{types::DiagnosticsProvider, Command, ListCommand},
        types::Error,
    },
    selectors,
};

/// Returns the selectors for a component whose url contains the `manifest` string.
pub async fn get_selectors_for_manifest<P: DiagnosticsProvider>(
    manifest: &Option<String>,
    tree_selectors: &Vec<String>,
    accessor_path: &Option<String>,
    provider: &P,
) -> Result<Vec<String>, Error> {
    match &manifest {
        None => Ok(tree_selectors.clone()),
        Some(manifest) => {
            let list_command = ListCommand {
                manifest: Some(manifest.clone()),
                with_url: false,
                accessor_path: accessor_path.clone(),
            };
            let monikers = list_command
                .execute(provider)
                .await?
                .into_iter()
                .map(|item| item.into_moniker())
                .collect::<Vec<_>>();
            if monikers.is_empty() {
                Err(Error::ManifestNotFound(manifest.clone()))
            } else if tree_selectors.is_empty() {
                Ok(monikers.into_iter().map(|moniker| format!("{}:root", moniker)).collect())
            } else {
                Ok(monikers
                    .into_iter()
                    .flat_map(|moniker| {
                        tree_selectors
                            .iter()
                            .map(move |tree_selector| format!("{}:{}", moniker, tree_selector))
                    })
                    .collect())
            }
        }
    }
}

/// Expand selectors.
pub fn expand_selectors(selectors: Vec<String>) -> Result<Vec<String>, Error> {
    let mut result = vec![];
    for selector in selectors {
        match selectors::tokenize_string(&selector, selectors::SELECTOR_DELIMITER) {
            Ok(tokens) => {
                if tokens.len() > 1 {
                    result.push(selector);
                } else if tokens.len() == 1 {
                    result.push(format!("{}:*", selector));
                } else {
                    return Err(Error::InvalidArguments(format!(
                        "Iquery selectors cannot be empty strings: {:?}",
                        selector
                    )));
                }
            }
            Err(e) => {
                return Err(Error::InvalidArguments(format!(
                    "Tokenizing a provided selector failed. Error: {:?} Selector: {:?}",
                    e, selector
                )));
            }
        }
    }
    Ok(result)
}
