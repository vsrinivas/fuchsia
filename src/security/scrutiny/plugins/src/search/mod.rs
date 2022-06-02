// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod controller;

use {
    crate::search::controller::{
        components::ComponentSearchController, manifests::ManifestSearchController,
        package_list::PackageListController, packages::PackageSearchController,
    },
    scrutiny::prelude::*,
    std::sync::Arc,
};

plugin!(
    SearchPlugin,
    PluginHooks::new(
        collectors! {},
        controllers! {
            "/search/components" => ComponentSearchController::default(),
            "/search/manifests" => ManifestSearchController::default(),
            "/search/packages" => PackageSearchController::default(),
            "/search/package/list" => PackageListController::default(),
        }
    ),
    vec![PluginDescriptor::new("CorePlugin")]
);

#[cfg(test)]
mod tests {
    use {
        crate::{
            core::collection::{
                testing::fake_component_src_pkg, Component, Components, Manifest, ManifestData,
                Manifests, Package, Packages,
            },
            search::controller::{
                components::{ComponentSearchController, ComponentSearchRequest},
                manifests::{ManifestSearchController, ManifestSearchRequest},
                packages::{PackageSearchController, PackageSearchRequest},
            },
        },
        fuchsia_merkle::{Hash, HASH_SIZE},
        fuchsia_url::{AbsolutePackageUrl, PackageName},
        scrutiny::model::{controller::DataController, model::DataModel},
        scrutiny_testing::{fake::fake_data_model, TEST_REPO_URL},
        serde_json::json,
        std::{collections::HashMap, path::PathBuf, str::FromStr, sync::Arc},
        url::Url,
    };

    fn data_model() -> Arc<DataModel> {
        fake_data_model()
    }

    #[fuchsia::test]
    fn test_component_search() {
        let model = data_model();
        let search = ComponentSearchController::default();
        model
            .set(Components::new(vec![Component {
                id: 0,
                url: Url::parse(
                    &AbsolutePackageUrl::new(
                        TEST_REPO_URL.clone(),
                        PackageName::from_str("foo").unwrap(),
                        None,
                        None,
                    )
                    .to_string(),
                )
                .unwrap(),
                version: 0,
                source: fake_component_src_pkg(),
            }]))
            .unwrap();
        let request_one = ComponentSearchRequest { url: "foo".to_string() };
        let request_two = ComponentSearchRequest { url: "bar".to_string() };
        let response_one: Vec<Component> =
            serde_json::from_value(search.query(model.clone(), json!(request_one)).unwrap())
                .unwrap();
        let response_two: Vec<Component> =
            serde_json::from_value(search.query(model.clone(), json!(request_two)).unwrap())
                .unwrap();
        assert_eq!(response_one.len(), 1);
        assert_eq!(response_two.len(), 0);
    }

    #[fuchsia::test]
    fn test_manifest_search() {
        let model = data_model();
        let search = ManifestSearchController::default();
        model
            .set(Manifests::new(vec![Manifest {
                component_id: 0,
                manifest: ManifestData::Version1("foo".to_string()),
                uses: vec![],
            }]))
            .unwrap();
        let request_one = ManifestSearchRequest { manifest: "foo".to_string() };
        let request_two = ManifestSearchRequest { manifest: "bar".to_string() };
        let response_one: Vec<Manifest> =
            serde_json::from_value(search.query(model.clone(), json!(request_one)).unwrap())
                .unwrap();
        let response_two: Vec<Manifest> =
            serde_json::from_value(search.query(model.clone(), json!(request_two)).unwrap())
                .unwrap();
        assert_eq!(response_one.len(), 1);
        assert_eq!(response_two.len(), 0);
    }

    #[fuchsia::test]
    fn test_package_search() {
        let model = data_model();
        let search = PackageSearchController::default();
        let mut contents = HashMap::new();
        contents.insert(PathBuf::from("foo"), Hash::from([0; HASH_SIZE]));
        model
            .set(Packages::new(vec![Package {
                name: PackageName::from_str("test_name").unwrap(),
                variant: None,
                merkle: Hash::from([0; HASH_SIZE]),
                contents,
                meta: HashMap::new(),
            }]))
            .unwrap();
        let request_one = PackageSearchRequest { files: "foo".to_string() };
        let request_two = PackageSearchRequest { files: "bar".to_string() };
        let response_one: Vec<Package> =
            serde_json::from_value(search.query(model.clone(), json!(request_one)).unwrap())
                .unwrap();
        let response_two: Vec<Package> =
            serde_json::from_value(search.query(model.clone(), json!(request_two)).unwrap())
                .unwrap();
        assert_eq!(response_one.len(), 1);
        assert_eq!(response_two.len(), 0);
    }
}
