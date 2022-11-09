// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::{Component, Components},
    anyhow::Result,
    regex::Regex,
    scrutiny::{
        model::controller::{DataController, HintDataType},
        model::model::*,
    },
    scrutiny_utils::usage::UsageBuilder,
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::sync::Arc,
};

#[derive(Deserialize, Serialize)]
pub struct ComponentSearchRequest {
    pub url: String,
}

#[derive(Default)]
pub struct ComponentSearchController {}

impl DataController for ComponentSearchController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: ComponentSearchRequest = serde_json::from_value(query)?;
        let mut response = Vec::<Component>::new();
        let url_re = Regex::new(&request.url)?;
        let components = &model.get::<Components>()?.entries;
        for component in components.iter() {
            if url_re.is_match(&component.url.to_string()) {
                response.push(component.clone());
            }
        }
        Ok(json!(response))
    }

    fn description(&self) -> String {
        "Searches for matching component urls across all components.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("search.components - Search for components with a given url pattern.")
            .summary("search.components --url fuchsia-pkg://some_url_pattern")
            .description(
                "Searches all the component urls and returns the components\
            that match the selected url pattern.
            ",
            )
            .arg("--url", "Searches for matching url components.")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--url".to_string(), HintDataType::NoType)]
    }
}
