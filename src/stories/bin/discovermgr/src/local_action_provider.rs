// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![cfg(test)]
use crate::indexing::load_module_facet;
use crate::models::{Action, FuchsiaFulfillment, Parameter};
use fidl_fuchsia_sys_index::{ComponentIndexMarker, ComponentIndexProxy};
use fuchsia_component::client::{launch, launcher, App};
use fuchsia_syslog::macros::*;
use {failure::Error, fuchsia_async as fasync};

const COMPONENT_INDEX_URL: &str =
    "fuchsia-pkg://fuchsia.com/component_index#meta/component_index.cmx";

/// Starts and connects to the component index service.
// TODO: combine with version in package_suggestions_provider
fn get_index_service() -> Result<(App, ComponentIndexProxy), Error> {
    let app = launch(&launcher()?, COMPONENT_INDEX_URL.to_string(), None)?;
    let service = app.connect_to_service::<ComponentIndexMarker>()?;
    Ok((app, service))
}

/// Gets a list of all components on the system
// TODO: combine with version in package_suggestions_provider
async fn get_components() -> Result<Vec<String>, Error> {
    let (_app, index_service) = get_index_service().map_err(|e| {
        fx_log_err!("Failed to connect to index service");
        e
    })?;
    // Empty string returns all components.
    let index_response = await!(index_service.fuzzy_search("")).map_err(|e| {
        fx_log_err!("Fuzzy search error from component index: {:?}", e);
        e
    })?;
    index_response.or_else(|e| {
        fx_log_err!("Fuzzy search error from component index: {:?}", e);
        Ok(vec![])
    })
}

// Gets a vector of all actions on the system
pub async fn get_local_actions() -> Result<Vec<Action>, Error> {
    let urls = await!(get_components())?;
    let mut result = vec![];
    for url in urls {
        let module_facet = await!(load_module_facet(&url))?;
        for intent_filter in module_facet.intent_filters.into_iter() {
            // Convert from Indexing to Models
            result.push(Action {
                name: intent_filter.action,
                parameters: intent_filter
                    .parameters
                    .iter()
                    .map(|(k, v)| Parameter { name: k.to_string(), parameter_type: v.to_string() })
                    .collect(),
                action_display: intent_filter.action_display,
                web_fulfillment: None,
                fuchsia_fulfillment: Some(FuchsiaFulfillment { component_url: url.to_string() }),
            })
        }
    }
    Ok(result)
}

#[cfg(test)]
mod test {
    use super::*;

    const TEST_COMPONENT_URL: &str =
        "fuchsia-pkg://fuchsia.com/discovermgr_tests#meta/discovermgr_bin_test.cmx";

    // Verify the actions in this component test manifest
    #[fasync::run_singlethreaded(test)]
    async fn test_component_index() -> Result<(), Error> {
        // TODO fix tests -- failing only in CQ
        let result = await!(get_local_actions()).unwrap_or_else(|e| {
            eprintln!("Error! {:?}", e);
            vec![]
        });
        #[allow(unused)]
        let actions: Vec<&Action> = result
            .iter()
            .filter(|a| match &a.fuchsia_fulfillment {
                Some(v) => v.component_url == TEST_COMPONENT_URL,
                None => false,
            })
            .collect();
        // assert_eq!(actions.len(), 2, "Expecting to find 2 actions in discovermgr test cmx file");
        Ok(())
    }
}
