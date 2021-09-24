// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Encapsulates the software distribution to host systems.
//!
//! Uses a unique identifier (FMS Name) to lookup SDK Module metadata.

use {
    anyhow::Result,
    sdk_metadata::{from_reader, Metadata},
    std::{collections::HashMap, io},
};

/// Manager for finding available SDK Modules for download or ensuring that an
/// SDK Module (artifact) is downloaded.
pub struct Entries {
    /// A collection of FMS Entries that have been added.
    data: HashMap<String, Metadata>,
}

impl Entries {
    pub fn new() -> Self {
        Self { data: HashMap::<_, _>::new() }
    }

    /// Add metadata from json text to the store of FMS entries.
    ///
    /// If the an entry exists with a given name already exists, the entry will
    /// be overwritten with the new metadata.
    pub fn add_json<R: io::Read>(&mut self, reader: &mut R) -> Result<()> {
        let metadata = from_reader(reader)?;
        self.data.insert(metadata.name().to_string(), metadata);
        Ok(())
    }

    /// Get metadata for a named entry.
    ///
    /// Returns None if no entry with 'name' is found.
    #[allow(unused)]
    pub fn entry(&self, name: &str) -> Option<&Metadata> {
        self.data.get(name)
    }

    /// Get first entry.
    ///
    /// Returns None if no entries exit.
    /// TODO(fxbug.dev/81756) - Remove when not needed.
    #[doc(hidden)]
    #[deprecated(note = "Use `entries.iter().next()` instead.")]
    pub fn first(&self) -> Option<&Metadata> {
        self.data.iter().next().map(|(_, value)| value)
    }

    /// Iterate over all entries.
    ///
    /// If the name is known, use `entry()` method instead since searching this
    /// iterator would be a brute force search.
    pub fn iter(&self) -> std::collections::hash_map::Values<'_, String, Metadata> {
        self.data.values()
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_entries() {
        use std::io::BufReader;
        let mut entries = Entries::new();
        const METADATA: &str = include_str!("../test_data/test_physical_device.json");

        // Damage the "type" key.
        let broken = str::replace(METADATA, r#""type""#, r#""wrong""#);
        let mut reader = BufReader::new(broken.as_bytes());
        match entries.add_json(&mut reader) {
            Ok(_) => panic!("badly formed metadata passed schema"),
            Err(_) => (),
        }
        assert_eq!(entries.entry("generic-x64"), None);

        let mut reader = BufReader::new(METADATA.as_bytes());
        // Not present until the json metadata is added.
        assert_eq!(entries.entry("generic-arm64"), None);
        entries.add_json(&mut reader).expect("add fms metadata");
        // Now it is present.
        assert_ne!(entries.entry("generic-arm64"), None);
        // Test first method.
        assert_eq!(entries.entry("generic-arm64"), entries.iter().next());
        // This isn't just returning non-None for everything.
        assert_eq!(entries.entry("unfound"), None);
    }

    #[test]
    fn test_product_bundle() {
        use std::io::BufReader;
        let mut entries = Entries::new();
        const METADATA: &str = include_str!("../test_data/test_product_bundle.json");

        // Damage Urls in the metadata.
        let broken = str::replace(METADATA, "https", "wrong");
        let mut reader = BufReader::new(broken.as_bytes());
        match entries.add_json(&mut reader) {
            Ok(_) => panic!("badly formed metadata passed schema"),
            Err(_) => (),
        }
        assert_eq!(entries.entry("generic-x64"), None);

        // Damage "kind" key.
        let broken = str::replace(METADATA, r#""type""#, r#""wrong""#);
        let mut reader = BufReader::new(broken.as_bytes());
        match entries.add_json(&mut reader) {
            Ok(_) => panic!("badly formed metadata passed schema"),
            Err(_) => (),
        }
        assert_eq!(entries.entry("generic-x64"), None);

        // Finally, add the correct (original) metadata.
        let mut reader = BufReader::new(METADATA.as_bytes());
        entries.add_json(&mut reader).expect("add fms metadata");
        assert_ne!(entries.entry("generic-x64"), None);
    }
}
