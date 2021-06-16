// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Encapsulates the software distribution to host systems.
//!
//! Uses a unique identifier (FMS Name) to lookup SDK Module metadata.

use {
    anyhow::{bail, format_err, Result},
    serde::Deserialize,
    serde_json::{from_reader, from_value, Value},
    std::{collections::HashMap, io},
};

/// Description of a physical (rather than virtual) hardware device.
#[derive(Debug, Deserialize, PartialEq, Clone)]
pub struct PhysicalDeviceSpec {
    /// A unique name identifying this FMS entry.
    pub name: String,

    /// Always "physical_device" for a PhysicalDeviceSpec. This is valuable for
    /// debugging or when writing this record to a json string.
    #[serde(rename = "type")]
    pub data_type: String,
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
}

impl Entries {
    pub fn new() -> Self {
        Self { data: HashMap::new() }
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
                    "name": "generic-arm64",
                    "type": "physical_device"
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
