// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::{Package, Packages},
    anyhow::Result,
    fuchsia_url::AbsolutePackageUrl,
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

static DEFAULT_HOST: &str = "fuchsia.com";

#[derive(Deserialize, Serialize)]
pub struct PackageListRequest {
    pub url: AbsolutePackageUrl,
}

impl PackageListRequest {
    fn url_matches_package(&self, pkg: &Package) -> bool {
        // Unconditionally match package name.
        self.url.name() == &pkg.name &&
            // Match variant iff request specifies variant.
            (self.url.variant().is_none()
                || self.url.variant() == pkg.variant.as_ref()) &&
            // Match hash iff request specifies hash.
            (self.url.hash().is_none()
                || self.url.hash() == Some(pkg.merkle))
    }
}

#[derive(Default)]
pub struct PackageListController {}

impl DataController for PackageListController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: PackageListRequest = serde_json::from_value(query)?;
        if request.url.host() != DEFAULT_HOST {
            warn!(
                request_url = %request.url.host(),
                default_host = %DEFAULT_HOST,
                "Package list controller URL contains non-default host which is not checked in URL matching",
            );
        }
        let packages = &model.get::<Packages>()?.entries;
        for package in packages.iter() {
            if request.url_matches_package(package) {
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
            .summary(&format!("search.package.list --url fuchsia-pkg://{}/foo", DEFAULT_HOST))
            .description("Lists all the files in a package.")
            .arg("--url", "The url of the package you want to extract.")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--url".to_string(), HintDataType::NoType)]
    }
}
