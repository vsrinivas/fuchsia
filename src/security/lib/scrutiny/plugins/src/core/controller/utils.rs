// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::collection::{Component, Components},
    anyhow::{Context, Error, Result},
    scrutiny::model::model::DataModel,
    serde::{Deserialize, Serialize},
    serde_json::{self, value::Value},
    std::io::{self, ErrorKind},
    std::sync::Arc,
    url::Url,
};

/// Converts a component_url to an internal component_id.
pub fn component_from_url(model: Arc<DataModel>, url: &Url) -> Option<Component> {
    if let Ok(components) = model.get::<Components>() {
        for component in components.iter() {
            if &component.url == url {
                return Some(component.clone());
            }
        }
    }
    None
}

/// A default component request contains either a url or a component_id. This
/// base request type provides some utility member functions around parsing
/// this type of common request.
#[derive(Deserialize, Serialize)]
pub struct DefaultComponentRequest {
    pub url: Option<String>,
    pub component_id: Option<Value>,
}

impl DefaultComponentRequest {
    /// Returns the component identifier handling the custom parsing of the
    /// component_id Value type and checking if a url or a component_id is set.
    /// If duplicates are found the url will always be selected.
    pub fn component_id(&self, model: Arc<DataModel>) -> Result<i64> {
        if let Some(url) = &self.url {
            let url = Url::parse(url)
                .with_context(|| format!("Failed to parse component URL: {}", url))?;
            if let Some(component) = component_from_url(model.clone(), &url) {
                Ok(component.id as i64)
            } else {
                return Err(Error::new(io::Error::new(
                    ErrorKind::Other,
                    format!("Could not find component with url: {}.", url),
                )));
            }
        } else if let Some(id) = &self.component_id {
            if id.is_i64() {
                let current_id = id.as_i64().unwrap();
                let components = &model.get::<Components>()?.entries;
                for component in components.iter() {
                    if component.id as i64 == current_id {
                        return Ok(id.as_i64().unwrap());
                    }
                }
                Err(Error::new(io::Error::new(
                    ErrorKind::Other,
                    format!("Unable to find component matching component_id: {}.", current_id),
                )))
            } else if id.is_string() {
                if let Ok(id) = id.as_str().unwrap().parse::<i64>() {
                    let components = &model.get::<Components>()?.entries;
                    for component in components.iter() {
                        if component.id as i64 == id {
                            return Ok(id);
                        }
                    }
                    Err(Error::new(io::Error::new(
                        ErrorKind::Other,
                        format!("Unable to find component matching component_id: {}.", id),
                    )))
                } else {
                    return Err(Error::new(io::Error::new(
                        ErrorKind::Other,
                        format!("Unable to parse component_id: {}.", id),
                    )));
                }
            } else {
                return Err(Error::new(io::Error::new(
                    ErrorKind::Other,
                    format!("Invalid component_id format received: {}.", id),
                )));
            }
        } else {
            return Err(Error::new(io::Error::new(
                ErrorKind::Other,
                format!("No url or component_id provided."),
            )));
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::DefaultComponentRequest,
        crate::core::collection::{
            testing::fake_component_src_pkg, Component, ComponentSource, Components,
        },
        scrutiny_testing::fake::*,
        serde_json::json,
        url::Url,
    };

    fn make_component(id: i32, url: &str, version: i32, source: ComponentSource) -> Component {
        let url = Url::parse(url).unwrap();
        Component { id, url, version, source }
    }

    #[fuchsia::test]
    fn default_component_request_component_id_int() {
        let model = fake_data_model();

        let comp = make_component(
            123,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake.cmx",
            0,
            fake_component_src_pkg(),
        );
        let mut components = Components::default();
        components.push(comp.clone());
        model.set(components).unwrap();

        let request = DefaultComponentRequest { component_id: Some(json!(123)), url: None };
        let component_id = request.component_id(model).unwrap();
        assert_eq!(component_id, 123);
    }

    #[fuchsia::test]
    fn default_component_request_component_id_string() {
        let model = fake_data_model();

        let comp = make_component(
            123,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake.cmx",
            0,
            fake_component_src_pkg(),
        );
        let mut components = Components::default();
        components.push(comp.clone());
        model.set(components).unwrap();

        let request = DefaultComponentRequest { component_id: Some(json!("123")), url: None };
        let component_id = request.component_id(model).unwrap();
        assert_eq!(component_id, 123);
    }

    #[fuchsia::test]
    fn default_component_request_component_url() {
        let model = fake_data_model();
        let comp = make_component(
            123,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake.cmx",
            0,
            fake_component_src_pkg(),
        );
        let mut components = Components::default();
        components.push(comp.clone());
        model.set(components).unwrap();
        let request = DefaultComponentRequest {
            component_id: None,
            url: Some("fuchsia-pkg://fuchsia.com/fake#meta/fake.cmx".to_string()),
        };
        let component_id = request.component_id(model).unwrap();
        assert_eq!(component_id, 123);
    }

    #[fuchsia::test]
    fn default_component_request_component_nothing_fails() {
        let model = fake_data_model();
        let comp = make_component(
            123,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake.cmx",
            0,
            fake_component_src_pkg(),
        );
        let mut components = Components::default();
        components.push(comp.clone());
        model.set(components).unwrap();
        let request = DefaultComponentRequest { component_id: None, url: None };
        assert!(request.component_id(model).is_err());
    }

    #[fuchsia::test]
    fn default_component_request_component_id_fails_if_not_available() {
        let model = fake_data_model();
        let comp = make_component(
            123,
            "fuchsia-pkg://fuchsia.com/fake#meta/fake.cmx",
            0,
            fake_component_src_pkg(),
        );
        let mut components = Components::default();
        components.push(comp.clone());
        model.set(components).unwrap();
        let request = DefaultComponentRequest { component_id: Some(json!(125)), url: None };
        assert!(request.component_id(model).is_err());
    }
}
