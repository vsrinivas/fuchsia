// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::Packages,
    anyhow::{anyhow, Context, Result},
    fuchsia_url::AbsolutePackageUrl,
    scrutiny::{model::controller::DataController, model::model::DataModel},
    scrutiny_utils::{url::from_pkg_url_parts, usage::UsageBuilder},
    serde_json::{self, value::Value},
    std::sync::Arc,
};

#[derive(Default)]
pub struct PackagesGraphController {}

impl DataController for PackagesGraphController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        let mut packages = model.get::<Packages>()?.entries.clone();
        packages.sort();
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
        let mut packages = model
            .get::<Packages>()
            .context("Failed to load collection Packages for PackageUrlListController")?
            .entries
            .clone();
        packages.sort();
        let package_urls = packages
            .into_iter()
            .map(|package| from_pkg_url_parts(package.name, package.variant, Some(package.merkle)))
            .collect::<Result<Vec<AbsolutePackageUrl>>>()
            .context(
                "Failed to construct AbsolutePackageUrl from Package for PackageUrlListController",
            )?;
        serde_json::to_value(package_urls).map_err(|err| {
            anyhow!(
                "Failed to serialize Vec<AbsolutePackageUrl> to JSON for PackageUrlListController: {:?}",
                err
            )
        })
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
        super::{PackageUrlListController, PackagesGraphController},
        crate::core::collection::{Package, Packages},
        fuchsia_merkle::HASH_SIZE,
        fuchsia_url::{PackageName, PackageVariant},
        scrutiny::model::controller::DataController,
        scrutiny_testing::fake::fake_data_model,
        serde_json::json,
        std::{collections::HashMap, str::FromStr},
    };

    static ZERO_MERKLE: [u8; HASH_SIZE] = [0; HASH_SIZE];

    fn pkg_name(name: &str) -> PackageName {
        PackageName::from_str(name).unwrap()
    }

    fn pkg_variant(variant: &str) -> PackageVariant {
        PackageVariant::from_str(variant).unwrap()
    }

    #[test]
    fn packages_test() {
        let model = fake_data_model();
        let packages_controller = PackagesGraphController::default();
        let mut packages = Packages::default();
        packages.push(Package {
            name: pkg_name("foo"),
            variant: Some(pkg_variant("bar")),
            merkle: ZERO_MERKLE.into(),
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
            name: pkg_name("foo"),
            variant: None,
            merkle: ZERO_MERKLE.into(),
            contents: HashMap::new(),
            meta: HashMap::new(),
        });
        model.set(packages).unwrap();
        let value = packages_controller.query(model, json!("")).unwrap();
        let response: Vec<String> = serde_json::from_value(value).unwrap();
        assert_eq!(response.len(), 1);
        assert_eq!(response[0], "fuchsia-pkg://fuchsia.com/foo?hash=0000000000000000000000000000000000000000000000000000000000000000".to_string());
    }
}
