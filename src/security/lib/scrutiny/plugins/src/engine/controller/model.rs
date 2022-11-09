// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::{Components, Manifests, Packages, Routes, Zbi},
    anyhow::Result,
    scrutiny::{model::controller::DataController, model::model::DataModel},
    scrutiny_utils::usage::UsageBuilder,
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::sync::Arc,
};

/// Displays basic stats from the model controller.
#[derive(Default)]
pub struct ModelStatsController {}

#[derive(Serialize, Deserialize)]
pub struct ModelStats {
    pub components: usize,
    pub packages: usize,
    pub manifests: usize,
    pub routes: usize,
    #[serde(rename = "zbi sections")]
    pub zbi_sections: usize,
    #[serde(rename = "bootfs files")]
    pub bootfs_files: usize,
}

impl DataController for ModelStatsController {
    fn query(&self, model: Arc<DataModel>, _query: Value) -> Result<Value> {
        let mut zbi_sections = 0;
        let mut bootfs_files = 0;
        let mut components_len = 0;
        let mut packages_len = 0;
        let mut manifests_len = 0;
        let mut routes_len = 0;

        if let Ok(zbi) = model.get::<Zbi>() {
            zbi_sections = zbi.sections.len();
            bootfs_files = zbi.bootfs.len();
        }
        if let Ok(components) = model.get::<Components>() {
            components_len = components.len();
        }
        if let Ok(packages) = model.get::<Packages>() {
            packages_len = packages.len();
        }
        if let Ok(manifests) = model.get::<Manifests>() {
            manifests_len = manifests.len();
        }
        if let Ok(routes) = model.get::<Routes>() {
            routes_len = routes.len();
        }

        let stats = ModelStats {
            components: components_len,
            packages: packages_len,
            manifests: manifests_len,
            routes: routes_len,
            zbi_sections,
            bootfs_files,
        };
        Ok(json!(stats))
    }

    fn description(&self) -> String {
        "Returns aggregated model statistics.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("engine.model.stats - Lists important model statistics")
            .summary("engine.model.stats")
            .description(
                "Lists the number of: components, packages, manifests, \
            routes, zbi sections and bootfs files currently loaded in the model.",
            )
            .build()
    }
}

/// Displays model environment information.
#[derive(Default)]
pub struct ModelConfigController {}

impl DataController for ModelConfigController {
    fn query(&self, model: Arc<DataModel>, _query: Value) -> Result<Value> {
        Ok(serde_json::to_value(model.config())?)
    }

    fn description(&self) -> String {
        "Returns the data model config.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("engine.model.config - Lists the model config")
            .summary("engine.model.config")
            .description("Lists important data about the model config.")
            .build()
    }
}
