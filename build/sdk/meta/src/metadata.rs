// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Metadata deserializer.

use crate::common::{ElementType, Envelope};
use crate::json::JsonObject;
use crate::physical_device::PhysicalDeviceV1;
use crate::product_bundle::ProductBundleV1;
use anyhow::{anyhow, Result};
use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::io::Read;

/// A unique schema identifier.
#[derive(Debug)]
enum SchemaId {
    PhysicalDeviceV1,
    ProductBundleV1,
}

lazy_static! {
    /// A map of schema ID URLs, values of $id JSON attributes, mapped to their
    /// enum values.
    static ref SCHEMA_IDS: HashMap<String, SchemaId> = {
        let mut m = HashMap::new();
        m.insert(
            Envelope::<PhysicalDeviceV1>::get_schema_id().unwrap(),
            SchemaId::PhysicalDeviceV1,
        );
        m.insert(Envelope::<ProductBundleV1>::get_schema_id().unwrap(), SchemaId::ProductBundleV1);
        m
    };
}

impl SchemaId {
    /// Returns a schema id corresponding to the ID string.
    pub fn from(schema_id: &String) -> Option<&Self> {
        SCHEMA_IDS.get(schema_id)
    }
}

/// Versioned metadata container.
#[derive(Debug, PartialEq)]
pub enum Metadata {
    PhysicalDeviceV1(PhysicalDeviceV1),
    ProductBundleV1(ProductBundleV1),
}

impl Metadata {
    /// Returns metadata entry name.
    pub fn name(&self) -> &str {
        match self {
            Self::PhysicalDeviceV1(data) => &data.name[..],
            Self::ProductBundleV1(data) => &data.name[..],
        }
    }
}

/// Envelope payload used for partial deserialization.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
struct Data {
    #[serde(rename = "type")]
    pub kind: ElementType,
}

impl JsonObject for Envelope<Data> {
    /// Returns an empty str since the this envelope is never validated against
    /// a schema.
    fn get_schema() -> &'static str {
        ""
    }
}

/// Deserializes JSON document into the appropriate metadata container.
///
/// The type resolution is done by the unique schema ID.
pub fn from_reader<R: Read>(mut source: R) -> Result<Metadata> {
    let mut buf = String::new();
    source.read_to_string(&mut buf)?;
    let envelope = Envelope::<Data>::new(buf.as_bytes())?;

    match SchemaId::from(&envelope.schema_id) {
        Some(schema_id) => {
            let metadata = match schema_id {
                SchemaId::PhysicalDeviceV1 => {
                    let e = Envelope::<PhysicalDeviceV1>::new(buf.as_bytes())?;
                    e.validate()?;
                    Metadata::PhysicalDeviceV1(e.data)
                }
                SchemaId::ProductBundleV1 => {
                    let e = Envelope::<ProductBundleV1>::new(buf.as_bytes())?;
                    e.validate()?;
                    Metadata::ProductBundleV1(e.data)
                }
            };
            Ok(metadata)
        }
        None => Err(anyhow!(
            "Unknown schema id {} for type {:?}.",
            &envelope.schema_id,
            envelope.data.kind
        )),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_read_physical_device_v1() {
        let json = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/physical_device-0bd5d21f.json",
            "data": {
                "name": "generic-x64",
                "type": "physical_device" ,
                "hardware": {
                   "cpu": {
                       "arch": "x64"
                   }
                }
            }
        }
        "#;
        let metadata = from_reader(json.as_bytes()).unwrap();
        match metadata {
            Metadata::PhysicalDeviceV1(data) => assert_eq!(data.name.as_str(), "generic-x64"),
            _ => assert!(false, "Unexpected metadata type {:?}", metadata),
        };
    }

    #[test]
    fn test_read_invalid_physical_device_v1() {
        // Missing required CPU arch.
        let json = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/physical_device-0bd5d21f.json",
            "data": {
                "name": "generic-x64",
                "type": "physical_device" ,
                "hardware": {
                }
            }
        }
        "#;
        let result = from_reader(json.as_bytes());
        assert!(result.is_err(), "Expected to fail validation.");
    }

    #[test]
    fn test_read_product_bundle_v1() {
        let json = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json",
            "data": {
                "name": "generic-x64",
                "type": "product_bundle",
                "device_refs": ["generic-x64"],
                "images": [{
                    "base_uri": "gs://fuchsia/development/0.20201216.2.1/images/generic-x64.tgz",
                    "format": "tgz"
                }],
                "packages": [{
                    "format": "tgz",
                    "repo_uri": "gs://fuchsia/development/0.20201216.2.1/packages/generic-x64.tar.gz"
                }]
            }
        }
        "#;
        let metadata = from_reader(json.as_bytes()).unwrap();
        match metadata {
            Metadata::ProductBundleV1(data) => assert_eq!(data.name.as_str(), "generic-x64"),
            _ => assert!(false, "Unexpected metadata type {:?}", metadata),
        };
    }

    #[test]
    fn test_read_invalid_product_bundle_v1() {
        // Missing required images and packages.
        let json = r#"
        {
            "schema_id": "http://fuchsia.com/schemas/sdk/product_bundle-6320eef1.json",
            "data": {
                "name": "generic-x64",
                "type": "product_bundle",
                "device_refs": ["generic-x64"],
            }
        }
        "#;
        let result = from_reader(json.as_bytes());
        assert!(result.is_err(), "Expected to fail validation.");
    }
}
