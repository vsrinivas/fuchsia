// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl_fuchsia_inspect::PropertyValue;
use fuchsia_zircon as zx;
use std::path::Path;

pub enum InspectValue {
    Text(String),
    Binary,
}

pub struct InspectProperty {
    pub key: String,
    pub value: InspectValue,
}

pub struct InspectObject {
    pub name: String,
    pub properties: Vec<InspectProperty>,
    pub child_inspect_objects: Vec<InspectObject>,
}

impl InspectObject {
    pub fn create(
        exclude_properties: &Vec<String>, client_channel: zx::Channel,
    ) -> Result<InspectObject, Error> {
        let mut inspect = fidl_fuchsia_inspect::InspectSynchronousProxy::new(client_channel);
        let obj = inspect.read_data(zx::Time::INFINITE)?;

        let mut inspect_properties = Vec::new();
        if let Some(properties) = obj.properties {
            for property in &properties {
                if exclude_properties.contains(&property.key) {
                    continue;
                }
                inspect_properties.push(InspectProperty {
                    key: property.key.clone(),
                    value: match &property.value {
                        PropertyValue::Str(s) => InspectValue::Text(s.clone()),
                        PropertyValue::Bytes(_) => InspectValue::Binary,
                    },
                });
            }
        }
        let mut child_inspect_objects = Vec::new();
        if let Some(children) = inspect.list_children(zx::Time::INFINITE)? {
            for child in &children {
                let (client, service) = zx::Channel::create()?;
                inspect.open_child(
                    child,
                    fidl::endpoints::ServerEnd::new(service),
                    zx::Time::INFINITE,
                )?;
                child_inspect_objects.push(InspectObject::create(exclude_properties, client)?);
            }
        }
        Ok(InspectObject {
            name: obj.name,
            properties: inspect_properties,
            child_inspect_objects: child_inspect_objects,
        })
    }
}

/// Given a path within the Hub to a component, this function generates an
/// Inspect object tree consistenting of properties that the component broadcasts
/// about itself.
pub fn generate_inspect_object_tree(
    path: &Path, exclude_properties: &Vec<String>,
) -> Result<InspectObject, Error> {
    let (client, service) = zx::Channel::create()?;
    fdio::service_connect(path.to_string_lossy().as_ref(), service)?;
    Ok(InspectObject::create(exclude_properties, client)?)
}
