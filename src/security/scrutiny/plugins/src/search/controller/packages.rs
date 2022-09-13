// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::{Package, Packages},
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
    tracing::warn,
};

#[derive(Deserialize, Serialize)]
pub struct PackageSearchRequest {
    pub files: String,
}

#[derive(Default)]
pub struct PackageSearchController {}

impl DataController for PackageSearchController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: PackageSearchRequest = serde_json::from_value(query)?;
        let mut response = Vec::<Package>::new();
        let file_re = Regex::new(&request.files)?;
        let packages = &model.get::<Packages>()?.entries;
        for package in packages.iter() {
            for (file_name, _blob) in package.contents.iter() {
                match file_name.to_str() {
                    Some(file_name) => {
                        if file_re.is_match(&file_name) {
                            response.push(package.clone());
                            break;
                        }
                    }
                    None => {
                        warn!(?file_name, "Failed to convert package-internal path to string");
                    }
                }
            }
        }
        Ok(json!(response))
    }

    fn description(&self) -> String {
        "Searches for matching file names in all packages.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("search.packages - Search for files in packages.")
            .summary("search.packages --files some_file_name ")
            .description(
                "Searches for specific file names within all the \
            indexed packages. This is useful if you want to track down which
            packages are using a certain shared library or which package a
            certain configuration file is in.",
            )
            .arg("--files", "The file to search for.")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--files".to_string(), HintDataType::NoType)]
    }
}
