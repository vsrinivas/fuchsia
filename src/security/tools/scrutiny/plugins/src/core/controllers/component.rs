// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::controllers::utils::DefaultComponentRequest,
    anyhow::{Error, Result},
    scrutiny::{
        model::controller::{DataController, HintDataType},
        model::model::{DataModel, ManifestData},
    },
    scrutiny_utils::usage::UsageBuilder,
    serde_json::{self, value::Value},
    std::io::{self, ErrorKind},
    std::sync::Arc,
};

#[derive(Default)]
pub struct ComponentsGraphController {}

impl DataController for ComponentsGraphController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        let mut components = model.components().read().unwrap().clone();
        components.sort_by(|a, b| a.url.partial_cmp(&b.url).unwrap());
        Ok(serde_json::to_value(components)?)
    }
    fn description(&self) -> String {
        "Returns every component indexed by the data model.".to_string()
    }
    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("components - Dumps every component indexed in the Data Model.")
            .summary("components")
            .description(
                "Dumps every component indexed by the Data Model. Note that \
            a single Fuchsia package can contain multiple components (cmx or cm) \
            manifests. This is intended for raw data analysis by piping this data \
            into a file.",
            )
            .build()
    }
}

#[derive(Default)]
pub struct ComponentGraphController {}

impl DataController for ComponentGraphController {
    fn query(&self, model: Arc<DataModel>, value: Value) -> Result<Value> {
        let req: DefaultComponentRequest = serde_json::from_value(value)?;
        let component_id = req.component_id(model.clone())?;
        let components = model.components().read().unwrap();
        for component in components.iter() {
            if component.id as i64 == component_id {
                return Ok(serde_json::to_value(component)?);
            }
        }
        Err(Error::new(io::Error::new(
            ErrorKind::Other,
            format!("Could not find a component with component_id {}.", component_id),
        )))
    }

    fn description(&self) -> String {
        "Returns a specific component given its internal id.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("component")
            .summary("component --url [URL_PATH] --component_id [COMPONENT_ID]")
            .description(
                "Returns all the information on a specific component \
            given its component_id or url.",
            )
            .arg("--component_id", "The component id for the component")
            .arg("--url", "The url for the component")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![
            ("--url".to_string(), HintDataType::NoType),
            ("--component_id".to_string(), HintDataType::NoType),
        ]
    }
}

#[derive(Default)]
pub struct ComponentManifestGraphController {}

impl DataController for ComponentManifestGraphController {
    fn query(&self, model: Arc<DataModel>, value: Value) -> Result<Value> {
        let req: DefaultComponentRequest = serde_json::from_value(value)?;
        let component_id = req.component_id(model.clone())?;

        let manifests = model.manifests().read().unwrap();
        for manifest in manifests.iter() {
            if manifest.component_id as i64 == component_id {
                if let ManifestData::Version1(data) = &manifest.manifest {
                    return Ok(serde_json::from_str(data)?);
                }
                if let ManifestData::Version2(data) = &manifest.manifest {
                    return Ok(serde_json::to_value(data.clone())?);
                }
            }
        }
        Err(Error::new(io::Error::new(ErrorKind::Other, format!("Could not find manifest"))))
    }

    fn description(&self) -> String {
        "Returns the raw manifest of a given component_id.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("components.manifest")
            .summary("components.manifest --url [COMPONENT_URL] --component_id [COMPONENT_ID]")
            .description("Returns a component manifest given its component_id or url.")
            .arg("--component_id", "The component id for the component")
            .arg("--url", "The url for the component")
            .build()
    }

    fn hints(&self) -> Vec<(String, HintDataType)> {
        vec![
            ("--url".to_string(), HintDataType::NoType),
            ("--component_id".to_string(), HintDataType::NoType),
        ]
    }
}

#[cfg(test)]
mod tests {

    use {super::*, scrutiny::model::model::*, serde_json::json, tempfile::tempdir};

    fn empty_value() -> Value {
        serde_json::from_str("{}").unwrap()
    }

    fn make_component(id: i32, url: &str, version: i32, inferred: bool) -> Component {
        Component { id: id, url: url.to_string(), version: version, inferred: inferred }
    }

    fn make_manifest(id: i32, manifest: &str) -> Manifest {
        Manifest {
            component_id: id,
            manifest: ManifestData::Version1(manifest.to_string()),
            uses: vec![],
        }
    }

    #[test]
    fn components_controller_returns_all_components() {
        let store_dir = tempdir().unwrap();
        let url = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(url).unwrap());

        let comp1 = make_component(1, "fake_url", 0, false);
        let comp2 = make_component(2, "fake_url_2", 0, true);
        {
            let mut components = model.components().write().unwrap();
            components.push(comp1.clone());
            components.push(comp2.clone());
        }

        let controller = ComponentsGraphController::default();
        let val = controller.query(model, empty_value()).unwrap();
        let response: Vec<Component> = serde_json::from_value(val).unwrap();

        assert_eq!(2, response.len());
        assert_eq!(comp1, response[0]);
        assert_eq!(comp2, response[1]);
    }

    #[test]
    fn component_id_controller_known_id_returns_component() {
        let store_dir = tempdir().unwrap();
        let url = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(url).unwrap());

        let comp_1 = make_component(1, "fake_url", 0, false);
        let comp_2 = make_component(2, "fake_url_2", 0, true);
        {
            let mut components = model.components().write().unwrap();
            components.push(comp_1.clone());
            components.push(comp_2.clone());
        }

        let controller = ComponentGraphController::default();
        let json_body = json!({
            "component_id": 1
        });
        let val = controller.query(model.clone(), json_body).unwrap();
        let response: Component = serde_json::from_value(val).unwrap();

        assert_eq!(comp_1, response);

        let json_body = json!({
            "url": "fake_url_2",
        });
        let val_2 = controller.query(model, json_body).unwrap();
        let response_2: Component = serde_json::from_value(val_2).unwrap();
        assert_eq!(comp_2, response_2);
    }

    #[test]
    fn component_id_controller_unknown_id_returns_err() {
        let store_dir = tempdir().unwrap();
        let url = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(url).unwrap());

        let comp1 = make_component(1, "fake_url", 0, false);
        let comp2 = make_component(2, "fake_url_2", 0, true);
        {
            let mut components = model.components().write().unwrap();
            components.push(comp1.clone());
            components.push(comp2.clone());
        }

        let controller = ComponentGraphController::default();
        let json_body = json!({
            "component_id": 3
        });
        assert!(controller.query(model, json_body).is_err());
    }

    #[test]
    fn component_raw_manifest_controller_known_id_returns_manifest() {
        let store_dir = tempdir().unwrap();
        let url = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(url).unwrap());

        let comp1 = make_component(1, "fake_url", 0, false);
        let comp2 = make_component(2, "fake_url_2", 0, true);
        {
            let mut components = model.components().write().unwrap();
            components.push(comp1.clone());
            components.push(comp2.clone());
        }

        let manifest1 = make_manifest(1, "{\"sandbox\": \"fake_manifest\"}");
        {
            let mut manifests = model.manifests().write().unwrap();
            manifests.push(manifest1.clone());
        }

        let controller = ComponentManifestGraphController::default();
        let request =
            DefaultComponentRequest { url: Some("fake_url".to_string()), component_id: None };
        let json_body = serde_json::to_value(request).unwrap();
        let val = controller.query(model, json_body).unwrap();
        assert_eq!(val.to_string().contains("fake_manifest"), true);
    }

    #[test]
    fn component_raw_manifest_controller_unknown_id_returns_err() {
        let store_dir = tempdir().unwrap();
        let url = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(url).unwrap());

        let comp1 = make_component(1, "fake_url", 0, false);
        let comp2 = make_component(2, "fake_url_2", 0, true);
        {
            let mut components = model.components().write().unwrap();
            components.push(comp1.clone());
            components.push(comp2.clone());
        }

        let manifest1 = make_manifest(1, "{\"fake_manifest\": \"fake_manifest\"");
        {
            let mut manifests = model.manifests().write().unwrap();
            manifests.push(manifest1.clone());
        }

        let controller = ComponentManifestGraphController::default();
        let json_body = json!({
            "component_id": 3
        });
        assert!(controller.query(model, json_body).is_err());
    }

    #[test]
    fn component_raw_manifest_controller_string_id_returns_manifest() {
        let store_dir = tempdir().unwrap();
        let url = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(url).unwrap());

        let comp1 = make_component(1, "fake_url", 0, false);
        let comp2 = make_component(2, "fake_url_2", 0, true);
        {
            let mut components = model.components().write().unwrap();
            components.push(comp1.clone());
            components.push(comp2.clone());
        }

        let manifest1 = make_manifest(1, "{\"sandbox\": \"fake_manifest\"}");
        {
            let mut manifests = model.manifests().write().unwrap();
            manifests.push(manifest1.clone());
        }

        let controller = ComponentManifestGraphController::default();
        let json_body = serde_json::to_value(DefaultComponentRequest {
            url: None,
            component_id: Some(json!("1")),
        })
        .unwrap();
        let val = controller.query(model, json_body).unwrap();
        assert_eq!(val.to_string().contains("fake_manifest"), true);
    }

    #[ignore]
    #[test]
    fn component_sandbox_controller_valid_id_invalid_sandbox_fields_returns_err() {
        assert!(true);
    }

    #[ignore]
    #[test]
    fn component_sandbox_controller_invalid_id_valid_sandbox_fields_returns_err() {
        assert!(true);
    }

    #[ignore]
    #[test]
    fn component_sandbox_controller_valid_id_valid_sandbox_fields_returns_only_requested_fields() {
        assert!(true);
    }
}
