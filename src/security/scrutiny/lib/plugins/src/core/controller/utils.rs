// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Error, Result},
    scrutiny::model::model::{Component, DataModel},
    serde::{Deserialize, Serialize},
    serde_json::{self, value::Value},
    std::io::{self, ErrorKind},
    std::sync::Arc,
};

/// Converts a component_url to an internal component_id.
pub fn component_from_url(model: Arc<DataModel>, url: &str) -> Option<Component> {
    let components = model.components().read().unwrap();
    for component in components.iter() {
        if component.url == url {
            return Some(component.clone());
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
                let components = model.components().read().unwrap();
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
                    let components = model.components().read().unwrap();
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
    use {super::*, serde_json::json, tempfile::tempdir};

    fn make_component(id: i32, url: &str, version: i32, inferred: bool) -> Component {
        Component { id: id, url: url.to_string(), version: version, inferred: inferred }
    }

    #[test]
    fn default_component_request_component_id_int() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let comp = make_component(123, "fake_url", 0, false);
        {
            let mut components = model.components().write().unwrap();
            components.push(comp.clone());
        }
        let request = DefaultComponentRequest { component_id: Some(json!(123)), url: None };
        let component_id = request.component_id(model).unwrap();
        assert_eq!(component_id, 123);
    }

    #[test]
    fn default_component_request_component_id_string() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let comp = make_component(123, "fake_url", 0, false);
        {
            let mut components = model.components().write().unwrap();
            components.push(comp.clone());
        }
        let request = DefaultComponentRequest { component_id: Some(json!("123")), url: None };
        let component_id = request.component_id(model).unwrap();
        assert_eq!(component_id, 123);
    }

    #[test]
    fn default_component_request_component_url() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let comp = make_component(123, "fake_url", 0, false);
        {
            let mut components = model.components().write().unwrap();
            components.push(comp.clone());
        }
        let request =
            DefaultComponentRequest { component_id: None, url: Some("fake_url".to_string()) };
        let component_id = request.component_id(model).unwrap();
        assert_eq!(component_id, 123);
    }

    #[test]
    fn default_component_request_component_nothing_fails() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let comp = make_component(123, "fake_url", 0, false);
        {
            let mut components = model.components().write().unwrap();
            components.push(comp.clone());
        }
        let request = DefaultComponentRequest { component_id: None, url: None };
        assert!(request.component_id(model).is_err());
    }

    #[test]
    fn default_component_request_component_id_fails_if_not_available() {
        let store_dir = tempdir().unwrap();
        let uri = store_dir.into_path().into_os_string().into_string().unwrap();
        let model = Arc::new(DataModel::connect(uri).unwrap());
        let comp = make_component(123, "fake_url", 0, false);
        {
            let mut components = model.components().write().unwrap();
            components.push(comp.clone());
        }
        let request = DefaultComponentRequest { component_id: Some(json!(125)), url: None };
        assert!(request.component_id(model).is_err());
    }
}
