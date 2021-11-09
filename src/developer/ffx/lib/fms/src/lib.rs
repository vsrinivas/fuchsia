// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Encapsulates the software distribution to host systems.
//!
//! Uses a unique identifier (FMS Name) to lookup SDK Module metadata.

use {
    anyhow::{anyhow, bail, Context, Result},
    glob::glob,
    sdk_metadata::{from_reader, Metadata, ProductBundleV1, VirtualDeviceV1},
    std::{
        collections::HashMap,
        fs::File,
        io::{BufReader, Read},
        path::Path,
    },
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

    /// Initialize the FMS database.
    ///
    /// Look for appropriate .json files in `dir_path` and import metadata from
    /// them.
    pub fn from_dir_path(dir_path: &Path) -> Result<Self> {
        let mut entries = Self::new();
        let all_json = dir_path.join("*.json");
        let dir_path_str = all_json.to_str().ok_or(anyhow!("Path to string failed."))?;
        for path in glob(&dir_path_str).context("Bad glob pattern.")? {
            let path = path?;
            let file = File::open(&path)?;
            let mut buf_reader = BufReader::new(file);
            entries.add_json(&mut buf_reader)?;
        }
        if entries.data.is_empty() {
            bail!("No valid FMS metadata was found in {:?}.", all_json);
        }
        Ok(entries)
    }

    /// Add metadata from json text to the store of FMS entries.
    ///
    /// If the an entry exists with a given name already exists, the entry will
    /// be overwritten with the new metadata.
    pub fn add_json<R: Read>(&mut self, reader: &mut R) -> Result<()> {
        let metadata = from_reader(reader)?;
        self.data.insert(metadata.name().to_string(), metadata);
        Ok(())
    }

    /// Get metadata for a named entry.
    ///
    /// Returns None if no entry with 'name' is found.
    pub fn entry(&self, name: &str) -> Option<&Metadata> {
        self.data.get(name)
    }

    /// Iterate over all entries.
    ///
    /// If the name is known, use `entry()` method instead since searching this
    /// iterator would be a brute force search.
    pub fn iter(&self) -> std::collections::hash_map::Values<'_, String, Metadata> {
        self.data.values()
    }
}

/// Retrieve the product bundle (PBM) from a given Entries set.
///
/// If `fms_name` is None and only one viable PBM is available, that entry is
/// returned.
///
/// Errors if no suitable PBM is found.
pub fn find_product_bundle<'a>(
    fms_entries: &'a Entries,
    fms_name: &Option<String>,
) -> Result<&'a ProductBundleV1> {
    if let Some(fms_name) = fms_name {
        if let Some(meta_pbm) = fms_entries.entry(fms_name) {
            match meta_pbm {
                Metadata::ProductBundleV1(pbm) => Ok(pbm),
                _ => bail!(
                    "The {:?} FMS entry is not a PBM. \
                    Please check the spelling and try again.",
                    fms_name
                ),
            }
        } else {
            bail!(
                "\
                The product bundle name {:?} was not found. \
                Please check the spelling and try again.",
                fms_name
            );
        }
    } else {
        let mut iter = fms_entries.iter().filter(|&x| matches!(x, Metadata::ProductBundleV1(_)));
        if let Some(Metadata::ProductBundleV1(pbm)) = iter.next() {
            if let None = iter.next() {
                // Since no specific PBM name was given, take the only
                // available PBM.
                Ok(pbm)
            } else {
                // In the future it would be better to filter through the
                // PBMs to see if only one entry would apply, then that one
                // would be a reasonable default. An error would still
                // happen if more than one PBM met all the filter criteria.
                bail!(
                    "\
                    There is more than one product bundle available. \
                    Please specify a product bundle by name."
                );
            }
        } else {
            bail!(
                "\
                    No valid product bundles were found. \
                    Consider creating them by building your product or \
                    updating the SDK."
            );
        }
    }
}

/// Retrieve a virtual device specification from a given Entries set.
///
/// `fms_name_list` is a list of fms_name strings to search for. The list may
///     contain both physical and virtual specs. The first virtual device spec
///     is returned.
///
/// Errors if no suitable Virtual Device Specification is found.
pub fn find_virtual_device<'a>(
    fms_entries: &'a Entries,
    fms_name_list: &[String],
) -> Result<&'a VirtualDeviceV1> {
    for fms_name in fms_name_list {
        if let Some(found) = fms_entries.entry(fms_name) {
            match found {
                Metadata::VirtualDeviceV1(device) => return Ok(device),
                _ => (),
            }
        }
    }
    bail!("No virtual device specification was found in {:?}.", fms_name_list);
}

#[cfg(test)]
mod tests {
    use {super::*, tempfile::TempDir};

    #[test]
    fn test_entries() {
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
    #[should_panic(expected = "No valid FMS metadata was found")]
    fn test_entries_from_dir_path_no_files() {
        let temp_dir = TempDir::new().expect("temp dir");
        Entries::from_dir_path(&temp_dir.path()).expect("load entries");
    }

    #[test]
    fn test_product_bundle() {
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

    #[test]
    #[should_panic(expected = "product bundle name \"fake\" was not found.")]
    fn test_find_product_bundle_panic() {
        let entries = Entries::new();
        find_product_bundle(&entries, &Some("fake".to_string())).expect("entry found");
    }

    #[test]
    fn test_find_product_bundle() {
        let mut entries = Entries::new();
        entries
            .add_json(&mut BufReader::new(
                include_str!("../test_data/test_product_bundle.json").as_bytes(),
            ))
            .expect("add metadata");
        find_product_bundle(&entries, &Some("generic-x64".to_string())).expect("entry found");
    }

    #[test]
    fn test_find_virtual_device() {
        let mut entries = Entries::new();
        entries
            .add_json(&mut BufReader::new(
                include_str!("../test_data/test_physical_device.json").as_bytes(),
            ))
            .expect("add metadata");
        entries
            .add_json(&mut BufReader::new(
                include_str!("../test_data/test_product_bundle.json").as_bytes(),
            ))
            .expect("add metadata");
        entries
            .add_json(&mut BufReader::new(
                include_str!("../test_data/test_virtual_device.json").as_bytes(),
            ))
            .expect("add metadata");
        let pbm =
            find_product_bundle(&entries, &Some("generic-x64".to_string())).expect("entry found");
        let device = find_virtual_device(&entries, &pbm.device_refs).expect("entry found");
        assert_eq!(device.name, "virtual-arm64");
    }
}
