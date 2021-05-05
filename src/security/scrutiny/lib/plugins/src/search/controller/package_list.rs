// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::Packages,
    anyhow::Result,
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
pub struct PackageListRequest {
    pub url: String,
}

#[derive(Default)]
pub struct PackageListController {}

impl DataController for PackageListController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: PackageListRequest = serde_json::from_value(query)?;
        let packages = &model.get::<Packages>()?.entries;
        for package in packages.iter() {
            if package.url == request.url {
                return Ok(json!(package.contents));
            }
        }
        Ok(json!({}))
    }

    fn description(&self) -> String {
        "Lists all the files in a package.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("search.package.list - List all the files in a package.")
            .summary("search.package.list --url fuchsia-pkg://fuchsia.com/foo")
            .description("Lists all the files in a package.")
            .arg("--url", "The url of the package you want to extract.")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--url".to_string(), HintDataType::NoType)]
    }
}
