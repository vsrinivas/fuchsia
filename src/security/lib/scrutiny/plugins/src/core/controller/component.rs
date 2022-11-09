// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::{
        collection::{Components, ManifestData, Manifests},
        controller::utils::DefaultComponentRequest,
        util::types::INFERRED_URL_SCHEME,
    },
    anyhow::{Error, Result},
    scrutiny::{
        model::controller::{DataController, HintDataType},
        model::model::DataModel,
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
        let mut components = model.get::<Components>()?.entries.clone();
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
        let components = model.get::<Components>()?;
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

        let manifests = model.get::<Manifests>()?;
        for manifest in manifests.iter() {
            if manifest.component_id as i64 == component_id {
                if let ManifestData::Version1(data) = &manifest.manifest {
                    return Ok(serde_json::from_str(data)?);
                }
                if let ManifestData::Version2 { cm_base64: data, .. } = &manifest.manifest {
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

#[derive(Default)]
pub struct ComponentsUrlListController {}

impl DataController for ComponentsUrlListController {
    fn query(&self, model: Arc<DataModel>, _: Value) -> Result<Value> {
        let mut components = model.get::<Components>()?.entries.clone();
        components.sort_by(|a, b| a.url.partial_cmp(&b.url).unwrap());
        let component_urls: Vec<String> = components
            .iter()
            .filter(|component| component.url.scheme() != INFERRED_URL_SCHEME)
            .map(|component| component.url.to_string())
            .collect();
        Ok(serde_json::to_value(component_urls)?)
    }

    fn description(&self) -> String {
        "Returns every component url in the data model.".to_string()
    }

    fn usage(&self) -> String {
        UsageBuilder::new()
            .name("components.urls - Dumps every component url in the Data Model.")
            .summary("components.urls")
            .description("Dumps every component url in the Data Model.")
            .build()
    }
}

#[cfg(test)]
mod tests {

    use {
        super::*,
        crate::core::collection::{
            testing::fake_component_src_pkg, Component, ComponentSource, Manifest,
        },
        scrutiny_testing::fake::*,
        serde_json::json,
        url::Url,
    };

    fn empty_value() -> Value {
        serde_json::from_str("{}").unwrap()
    }

    fn make_component(id: i32, url: &str, version: i32, source: ComponentSource) -> Component {
        let url = Url::parse(url).unwrap();
        Component { id, url, version, source }
    }

    fn make_manifest(id: i32, manifest: &str) -> Manifest {
        Manifest {
            component_id: id,
            manifest: ManifestData::Version1(manifest.to_string()),
            uses: vec![],
        }
    }

    #[fuchsia::test]
    fn components_controller_returns_all_components() {
        let model = fake_data_model();

        let comp1 = make_component(
            1,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake.cmx",
            0,
            fake_component_src_pkg(),
        );
        let comp2 = make_component(
            2,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake_2.cmx",
            0,
            ComponentSource::Inferred,
        );
        let mut components = Components::default();
        components.push(comp1.clone());
        components.push(comp2.clone());
        model.set(components).unwrap();

        let controller = ComponentsGraphController::default();
        let val = controller.query(model, empty_value()).unwrap();
        let response: Vec<Component> = serde_json::from_value(val).unwrap();

        assert_eq!(2, response.len());
        assert_eq!(comp1, response[0]);
        assert_eq!(comp2, response[1]);
    }

    #[fuchsia::test]
    fn component_id_controller_known_id_returns_component() {
        let model = fake_data_model();

        let comp_1 = make_component(
            1,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake.cmx",
            0,
            fake_component_src_pkg(),
        );
        let comp_2 = make_component(
            2,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake_2.cmx",
            0,
            ComponentSource::Inferred,
        );
        let mut components = Components::default();
        components.push(comp_1.clone());
        components.push(comp_2.clone());
        model.set(components).unwrap();

        let controller = ComponentGraphController::default();
        let json_body = json!({
            "component_id": 1
        });
        let val = controller.query(model.clone(), json_body).unwrap();
        let response: Component = serde_json::from_value(val).unwrap();

        assert_eq!(comp_1, response);

        let json_body = json!({
            "url": "fuchsia-pkg://fuchsia.com/fake#meta/fake_2.cmx",
        });
        let val_2 = controller.query(model, json_body).unwrap();
        let response_2: Component = serde_json::from_value(val_2).unwrap();
        assert_eq!(comp_2, response_2);
    }

    #[fuchsia::test]
    fn component_id_controller_unknown_id_returns_err() {
        let model = fake_data_model();

        let comp1 = make_component(
            1,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake.cmx",
            0,
            fake_component_src_pkg(),
        );
        let comp2 = make_component(
            2,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake_2.cmx",
            0,
            ComponentSource::Inferred,
        );
        let mut components = Components::default();
        components.push(comp1.clone());
        components.push(comp2.clone());
        model.set(components).unwrap();

        let controller = ComponentGraphController::default();
        let json_body = json!({
            "component_id": 3
        });
        assert!(controller.query(model, json_body).is_err());
    }

    #[fuchsia::test]
    fn component_raw_manifest_controller_known_id_returns_manifest() {
        let model = fake_data_model();

        let comp1 = make_component(
            1,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake.cmx",
            0,
            fake_component_src_pkg(),
        );
        let comp2 = make_component(
            2,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake_2.cmx",
            0,
            ComponentSource::Inferred,
        );
        let mut components = Components::default();
        components.push(comp1.clone());
        components.push(comp2.clone());
        model.set(components).unwrap();

        let manifest1 = make_manifest(1, "{\"sandbox\": \"fake_manifest\"}");
        let mut manifests = Manifests::default();
        manifests.push(manifest1.clone());
        model.set(manifests).unwrap();

        let controller = ComponentManifestGraphController::default();
        let request = DefaultComponentRequest {
            url: Some("fuchsia-pkg://fuchsia.com/fake#meta/fake.cmx".to_string()),
            component_id: None,
        };
        let json_body = serde_json::to_value(request).unwrap();
        let val = controller.query(model, json_body).unwrap();
        assert_eq!(val.to_string().contains("fake_manifest"), true);
    }

    #[fuchsia::test]
    fn component_raw_manifest_controller_unknown_id_returns_err() {
        let model = fake_data_model();

        let comp1 = make_component(
            1,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake.cmx",
            0,
            fake_component_src_pkg(),
        );
        let comp2 = make_component(
            2,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake_2.cmx",
            0,
            ComponentSource::Inferred,
        );
        let mut components = Components::default();
        components.push(comp1.clone());
        components.push(comp2.clone());
        model.set(components).unwrap();

        let manifest1 = make_manifest(1, "{\"fake_manifest\": \"fake_manifest\"");
        let mut manifests = Manifests::default();
        manifests.push(manifest1.clone());
        model.set(manifests).unwrap();

        let controller = ComponentManifestGraphController::default();
        let json_body = json!({
            "component_id": 3
        });
        assert!(controller.query(model, json_body).is_err());
    }

    #[fuchsia::test]
    fn component_raw_manifest_controller_string_id_returns_manifest() {
        let model = fake_data_model();

        let comp1 = make_component(
            1,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake.cmx",
            0,
            fake_component_src_pkg(),
        );
        let comp2 = make_component(
            2,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake_2.cmx",
            0,
            ComponentSource::Inferred,
        );
        let mut components = Components::default();
        components.push(comp1.clone());
        components.push(comp2.clone());
        model.set(components).unwrap();

        let manifest1 = make_manifest(1, "{\"sandbox\": \"fake_manifest\"}");
        let mut manifests = Manifests::default();
        manifests.push(manifest1.clone());
        model.set(manifests).unwrap();

        let controller = ComponentManifestGraphController::default();
        let json_body = serde_json::to_value(DefaultComponentRequest {
            url: None,
            component_id: Some(json!("1")),
        })
        .unwrap();
        let val = controller.query(model, json_body).unwrap();
        assert_eq!(val.to_string().contains("fake_manifest"), true);
    }

    #[fuchsia::test]
    fn component_urls() {
        let model = fake_data_model();

        let comp1 = make_component(
            1,
            "fuchsia-pkg://fuchsia.com/bar#meta/bar.cm",
            0,
            ComponentSource::Inferred,
        );
        let comp2 = make_component(
            2,
            "fuchsia-pkg://fuchsia.com/foo#meta/foo.cm",
            0,
            fake_component_src_pkg(),
        );
        let mut components = Components::default();
        components.push(comp1.clone());
        components.push(comp2.clone());
        model.set(components).unwrap();

        let controller = ComponentsUrlListController::default();
        let val = controller.query(model, empty_value()).unwrap();
        let response: Vec<String> = serde_json::from_value(val).unwrap();

        assert_eq!(2, response.len());
        assert_eq!(comp1.url.to_string(), response[0]);
        assert_eq!(comp2.url.to_string(), response[1]);
    }

    #[ignore]
    #[fuchsia::test]
    fn component_sandbox_controller_valid_id_invalid_sandbox_fields_returns_err() {
        assert!(true);
    }

    #[ignore]
    #[fuchsia::test]
    fn component_sandbox_controller_invalid_id_valid_sandbox_fields_returns_err() {
        assert!(true);
    }

    #[ignore]
    #[fuchsia::test]
    fn component_sandbox_controller_valid_id_valid_sandbox_fields_returns_only_requested_fields() {
        assert!(true);
    }
}
