// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::{Manifest, ManifestData, Manifests},
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
pub struct ManifestSearchRequest {
    pub manifest: String,
}

#[derive(Default)]
pub struct ManifestSearchController {}

impl DataController for ManifestSearchController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: ManifestSearchRequest = serde_json::from_value(query)?;
        let mut response = Vec::<Manifest>::new();
        let manifest_re = Regex::new(&request.manifest)?;
        let manifests = &model.get::<Manifests>()?.entries;
        for manifest in manifests.iter() {
            if let ManifestData::Version1(data) = &manifest.manifest {
                if manifest_re.is_match(&data) {
                    response.push(manifest.clone());
                }
            }
        }
        Ok(json!(response))
    }

    fn description(&self) -> String {
        "Searches for matching manifest file names in all packages.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("search.manifests - Search matching manifest patterns.")
            .summary("search.manifests --manifest deprecated_feature")
            .description(
                "Searches for a specific pattern or protocol used in \
            CMX files. This is useful if you want to search for all the manifests \
            that contain a certain feature, protocol or pattern.",
            )
            .arg("--manifest", "The manifest pattern to search for.")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--manifest".to_string(), HintDataType::NoType)]
    }
}
