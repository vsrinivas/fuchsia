// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Encapsulates the software distribution to host systems.
//!
//! Uses a unique identifier (FMS Name) to lookup SDK Module metadata.

use {
    anyhow::{bail, format_err, Result},
    serde::Deserialize,
    serde_json::{from_reader, from_str, from_value, Value},
    std::{collections::HashMap, io},
    valico::json_schema,
};

/// Description of a physical (rather than virtual) hardware device.
#[derive(Debug, Deserialize, PartialEq, Clone)]
pub struct PhysicalDeviceSpec {
    /// A unique name identifying this FMS entry.
    pub name: String,

    /// Always "physical_device" for a PhysicalDeviceSpec. This is valuable for
    /// debugging or when writing this record to a json string.
    pub kind: String,
}

/// Description of the data needed to set up (flash) a device.
#[derive(Debug, Deserialize, PartialEq, Clone)]
pub struct ProductBundle {
    /// A unique name identifying this FMS entry.
    pub name: String,

    /// Always "product_bundle" for a ProductBundle. This is valuable for
    /// debugging or when writing this record to a json string.
    #[serde(rename = "type")]
    pub data_type: String,
}

// A metadata record about a physical/virtual device, product bundle, etc.
#[derive(Debug, PartialEq, Clone)]
pub enum Entry {
    Physical(PhysicalDeviceSpec),
    Product(ProductBundle),
}

/// Manager for finding available SDK Modules for download or ensuring that an
/// SDK Module (artifact) is downloaded.
pub struct Entries {
    /// A collection of FMS Entries that have been added.
    data: HashMap<String, Entry>,

    /// Collection of schema to validate new data against. The key is the value
    /// of the "version" field. Version is a hash so it incorporates both the
    /// type and the revision.
    schemata: HashMap<String, serde_json::Value>,
}

impl Entries {
    pub fn new() -> Self {
        let mut schemata = HashMap::new();

        let input: Value =
            from_str(include_str!("../../../../../../build/sdk/meta/physical_device.json"))
                .expect("unicode string");
        let key = input["id"].as_str().expect("need id for schema");
        schemata.insert(key.to_string(), input);

        let input: Value =
            from_str(include_str!("../../../../../../build/sdk/meta/product_bundle.json"))
                .expect("unicode string");
        let key = input["id"].as_str().expect("need id for schema");
        schemata.insert(key.to_string(), input);

        Self { data: HashMap::new(), schemata }
    }

    /// Add metadata from json text to the store of FMS entries.
    ///
    /// If the an entry exists with a given name already exists, the entry will
    /// be overwritten with the new metadata.
    pub fn add_json<R>(&mut self, reader: &mut R) -> Result<()>
    where
        R: io::Read,
    {
        let input: Value = from_reader(reader)?;

        // Which schema should this follow.
        let schema_id: &str =
            input["version"].as_str().ok_or(format_err!("Missing version string"))?;

        // Validate the input against the schema.
        let mut scope = json_schema::Scope::new();

        let common_schema = from_str(include_str!("../../../../../../build/sdk/meta/common.json"))?;
        scope
            .compile(common_schema, /*ban_unknown=*/ true)
            .map_err(|e| format_err!("common.json failed to compile: {:?}", e))?;

        let common_schema =
            from_str(include_str!("../../../../../../build/sdk/meta/hardware.json"))?;
        scope
            .compile(common_schema, /*ban_unknown=*/ true)
            .map_err(|e| format_err!("hardware.json failed to compile: {:?}", e))?;

        let schema = self
            .schemata
            .get(schema_id)
            .ok_or(format_err!("No matching schema for version: {:?}", input["version"]))?;
        let compiled_schema = scope
            .compile_and_return(schema.to_owned(), /*ban_unknown=*/ true)
            .map_err(|e| format_err!(format!("Couldn't parse schema '{}': {:?}", schema_id, e)))?;
        let validation = compiled_schema.validate(&input);
        if !validation.is_strictly_valid() {
            let mut errors = Vec::new();
            for error in &validation.errors {
                errors.push(
                    format!("\n    {} at {}", error.get_title(), error.get_path()).into_boxed_str(),
                );
            }
            for missing in &validation.missing {
                errors.push(
                    format!("\n    schema definition is missing URL {}", missing).into_boxed_str(),
                );
            }
            errors.sort_unstable();
            bail!("{} {}", schema_id, errors.join(""));
        }

        // Create a new FMS Entry.
        let entry: Entry = match schema_id {
            "http://fuchsia.com/schemas/sdk/physical_device.json" => {
                Entry::Physical(from_value::<PhysicalDeviceSpec>(input["data"].to_owned())?)
            }
            "http://fuchsia.com/schemas/sdk/product_bundle-02.json" => {
                Entry::Product(from_value::<ProductBundle>(input["data"].to_owned())?)
            }
            _ => bail!("No matching struct for: {:?}", input["version"]),
        };

        // Store the new Entry using the entry's name as a key.
        let key = input["data"]["name"].as_str().expect("need name for FMS Entry");
        self.data.insert(key.to_string(), entry);
        Ok(())
    }

    /// Get metadata for a named entry.
    ///
    /// Returns None if no entry with 'name' is found.
    #[allow(unused)]
    pub fn entry(&self, name: &str) -> Option<&Entry> {
        self.data.get(name)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_entries() {
        use std::io::BufReader;
        let mut entries = Entries::new();
        let mut reader = BufReader::new(
            r#"
            {
                "data": {
                    "description": "A generic arm64 device",
                    "hardware": {
                        "cpu": {
                            "arch": "arm64"
                        }
                    },
                    "kind": "physical_device",
                    "name": "generic-arm64"
                },
                "version": "http://fuchsia.com/schemas/sdk/physical_device.json"
            }
            "#
            .as_bytes(),
        );
        // Not present until the json metadata is added.
        assert_eq!(entries.entry("generic-arm64"), None);
        entries.add_json(&mut reader).expect("add fms metadata");
        // Now it is present.
        assert_ne!(entries.entry("generic-arm64"), None);
        // This isn't just returning non-None for everything.
        assert_eq!(entries.entry("unfound"), None);
    }
}
