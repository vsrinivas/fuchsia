// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::Packages,
    anyhow::Result,
    scrutiny::{model::controller::DataController, model::model::DataModel},
    scrutiny_utils::usage::UsageBuilder,
    serde_json::{self, value::Value},
    std::sync::Arc,
};

#[derive(Default)]
pub struct PackagesGraphController {}

impl DataController for PackagesGraphController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        let mut packages = model.get::<Packages>()?.entries.clone();
        packages.sort_by(|a, b| a.url.partial_cmp(&b.url).unwrap());
        Ok(serde_json::to_value(packages)?)
    }

    fn description(&self) -> String {
        "Returns all Fuchsia packages and their file index.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("packages - Dumps all of the package data in the model")
            .summary("packages")
            .description(
                "Dumps all of the metadata for every package in the \
            Data Model. This includes all the files in the package and the \
            package url for every package indexed.",
            )
            .build()
    }
}

#[derive(Default)]
pub struct PackageUrlListController {}

impl DataController for PackageUrlListController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        let mut packages = model.get::<Packages>()?.entries.clone();
        packages.sort_by(|a, b| a.url.partial_cmp(&b.url).unwrap());
        let package_urls: Vec<String> = packages.iter().map(|e| e.url.clone()).collect();
        Ok(serde_json::to_value(package_urls)?)
    }

    fn description(&self) -> String {
        "Lists all the package urls in the model.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("packages.url - Dumps all the package urls found in the model.")
            .summary("packages.url")
            .description("Dumps all the package urls found in the model.")
            .build()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::core::collection::Package, scrutiny_testing::fake::*, serde_json::json,
        std::collections::HashMap,
    };

    #[test]
    fn packages_test() {
        let model = fake_data_model();
        let packages_controller = PackagesGraphController::default();
        let mut packages = Packages::default();
        packages.push(Package {
            url: "foo".to_string(),
            merkle: "bar".to_string(),
            contents: HashMap::new(),
            meta: HashMap::new(),
        });
        model.set(packages).unwrap();
        let value = packages_controller.query(model, json!("")).unwrap();
        let response: Vec<Package> = serde_json::from_value(value).unwrap();
        assert_eq!(response.len(), 1);
    }

    #[test]
    fn packages_url_test() {
        let model = fake_data_model();
        let packages_controller = PackageUrlListController::default();
        let mut packages = Packages::default();
        packages.push(Package {
            url: "fuchsia-pkg://fuchsia.com/foo".to_string(),
            merkle: "bar".to_string(),
            contents: HashMap::new(),
            meta: HashMap::new(),
        });
        model.set(packages).unwrap();
        let value = packages_controller.query(model, json!("")).unwrap();
        let response: Vec<String> = serde_json::from_value(value).unwrap();
        assert_eq!(response.len(), 1);
        assert_eq!(response[0], "fuchsia-pkg://fuchsia.com/foo".to_string());
    }
}
