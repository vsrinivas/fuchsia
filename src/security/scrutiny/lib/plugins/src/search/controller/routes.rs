// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::{Route, Routes},
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
pub struct RouteSearchRequest {
    pub service_name: String,
}

#[derive(Default)]
pub struct RouteSearchController {}

impl DataController for RouteSearchController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: RouteSearchRequest = serde_json::from_value(query)?;
        let mut response = Vec::<Route>::new();
        let service_re = Regex::new(&request.service_name)?;
        let routes = &model.get::<Routes>()?.entries;
        for route in routes.iter() {
            if service_re.is_match(&route.service_name) {
                response.push(route.clone());
            }
        }
        Ok(json!(response))
    }

    fn description(&self) -> String {
        "Searches for matching service names across all routes.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("search.routes - Searchs all the routes for a particular service.")
            .summary("search.routes --service_name Some.Service.Name")
            .description(
                "Searches all of the routes indexed for a particular service \
            name usage.
            ",
            )
            .arg("--service_name", "Searches all the routes for usage of the service name")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--service_name".to_string(), HintDataType::NoType)]
    }
}
