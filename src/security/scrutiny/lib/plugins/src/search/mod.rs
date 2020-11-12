// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod controller;

use {
    crate::search::controller::{
        components::ComponentSearchController, manifests::ManifestSearchController,
        packages::PackageSearchController, routes::RouteSearchController,
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
            "/search/routes" => RouteSearchController::default(),
        }
    ),
    vec![PluginDescriptor::new("CorePlugin")]
);

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::core::collection::{
            Component, Components, Manifest, ManifestData, Manifests, Package, Packages, Route,
            Routes,
        },
        crate::search::controller::{
            components::ComponentSearchRequest, manifests::ManifestSearchRequest,
            packages::PackageSearchRequest, routes::RouteSearchRequest,
        },
        serde_json::json,
        std::collections::HashMap,
        tempfile::tempdir,
    };

    fn data_model() -> Arc<DataModel> {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        Arc::new(DataModel::connect(uri).unwrap())
    }

    #[test]
    fn test_component_search() {
        let model = data_model();
        let search = ComponentSearchController::default();
        model
            .set(Components {
                entries: vec![Component {
                    id: 0,
                    url: "foo".to_string(),
                    version: 0,
                    inferred: false,
                }],
            })
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

    #[test]
    fn test_manifest_search() {
        let model = data_model();
        let search = ManifestSearchController::default();
        model
            .set(Manifests {
                entries: vec![Manifest {
                    component_id: 0,
                    manifest: ManifestData::Version1("foo".to_string()),
                    uses: vec![],
                }],
            })
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

    #[test]
    fn test_package_search() {
        let model = data_model();
        let search = PackageSearchController::default();
        let mut contents = HashMap::new();
        contents.insert("foo".to_string(), "bar".to_string());
        model
            .set(Packages {
                entries: vec![Package {
                    url: "test_url".to_string(),
                    merkle: "test_merkle".to_string(),
                    contents,
                }],
            })
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

    #[test]
    fn test_route_search() {
        let model = data_model();
        let search = RouteSearchController::default();
        model
            .set(Routes {
                entries: vec![Route {
                    id: 0,
                    src_id: 1,
                    dst_id: 2,
                    service_name: "foo".to_string(),
                    protocol_id: 0,
                }],
            })
            .unwrap();
        let request_one = RouteSearchRequest { service_name: "foo".to_string() };
        let request_two = RouteSearchRequest { service_name: "bar".to_string() };
        let response_one: Vec<Route> =
            serde_json::from_value(search.query(model.clone(), json!(request_one)).unwrap())
                .unwrap();
        let response_two: Vec<Route> =
            serde_json::from_value(search.query(model.clone(), json!(request_two)).unwrap())
                .unwrap();
        assert_eq!(response_one.len(), 1);
        assert_eq!(response_two.len(), 0);
    }
}
