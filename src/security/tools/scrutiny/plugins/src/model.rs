// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    regex::Regex,
    scrutiny::{
        collectors, controllers,
        engine::hook::PluginHooks,
        engine::plugin::{Plugin, PluginDescriptor},
        model::collector::DataCollector,
        model::controller::{DataController, HintDataType},
        model::model::*,
        plugin,
    },
    serde::{Deserialize, Serialize},
    serde_json::{json, value::Value},
    std::sync::Arc,
};

#[derive(Deserialize, Serialize)]
struct ComponentSearchRequest {
    url: String,
}

#[derive(Default)]
pub struct ComponentSearchController {}

impl DataController for ComponentSearchController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: ComponentSearchRequest = serde_json::from_value(query)?;
        let mut response = Vec::<Component>::new();
        let url_re = Regex::new(&request.url)?;
        let components = model.components().read().unwrap();
        for component in components.iter() {
            if url_re.is_match(&component.url) {
                response.push(component.clone());
            }
        }
        Ok(json!(response))
    }

    fn description(&self) -> String {
        "Searches for matching component urls across all components.".to_string()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--url".to_string(), HintDataType::NoType)]
    }
}

#[derive(Deserialize, Serialize)]
struct ManifestSearchRequest {
    manifest: String,
}

#[derive(Default)]
pub struct ManifestSearchController {}

impl DataController for ManifestSearchController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: ManifestSearchRequest = serde_json::from_value(query)?;
        let mut response = Vec::<Manifest>::new();
        let manifest_re = Regex::new(&request.manifest)?;
        let manifests = model.manifests().read().unwrap();
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

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--manifest".to_string(), HintDataType::NoType)]
    }
}

#[derive(Deserialize, Serialize)]
struct PackageSearchRequest {
    files: String,
}

#[derive(Default)]
pub struct PackageSearchController {}

impl DataController for PackageSearchController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: PackageSearchRequest = serde_json::from_value(query)?;
        let mut response = Vec::<Package>::new();
        let file_re = Regex::new(&request.files)?;
        let packages = model.packages().read().unwrap();
        for package in packages.iter() {
            for (file_name, _blob) in package.contents.iter() {
                if file_re.is_match(&file_name) {
                    response.push(package.clone());
                    break;
                }
            }
        }
        Ok(json!(response))
    }

    fn description(&self) -> String {
        "Searches for matching file names in all packages.".to_string()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--files".to_string(), HintDataType::NoType)]
    }
}

#[derive(Deserialize, Serialize)]
struct RouteSearchRequest {
    service_name: String,
}

#[derive(Default)]
pub struct RouteSearchController {}

impl DataController for RouteSearchController {
    fn query(&self, model: Arc<DataModel>, query: Value) -> Result<Value> {
        let request: RouteSearchRequest = serde_json::from_value(query)?;
        let mut response = Vec::<Route>::new();
        let service_re = Regex::new(&request.service_name)?;
        let routes = model.routes().read().unwrap();
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

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![("--service_name".to_string(), HintDataType::NoType)]
    }
}

plugin!(
    ModelPlugin,
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
    use {super::*, std::collections::HashMap, tempfile::tempdir};

    fn data_model() -> Arc<DataModel> {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        Arc::new(DataModel::connect(uri).unwrap())
    }

    #[test]
    fn test_component_search() {
        let model = data_model();
        let search = ComponentSearchController::default();
        model.components().write().unwrap().push(Component {
            id: 0,
            url: "foo".to_string(),
            version: 0,
            inferred: false,
        });
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
        model.manifests().write().unwrap().push(Manifest {
            component_id: 0,
            manifest: ManifestData::Version1("foo".to_string()),
            uses: vec![],
        });
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
        model.packages().write().unwrap().push(Package {
            url: "test_url".to_string(),
            merkle: "test_merkle".to_string(),
            contents,
        });
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
        model.routes().write().unwrap().push(Route {
            id: 0,
            src_id: 1,
            dst_id: 2,
            service_name: "foo".to_string(),
            protocol_id: 0,
        });
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
