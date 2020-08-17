// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::{jsons::*, package_getter::*, types::*, util},
    anyhow::Result,
    fuchsia_archive::Reader as FarReader,
    std::collections::HashMap,
    std::fs::File,
    std::io::{BufReader, Cursor},
    std::str,
};

// Constants/Statics
pub const CF_V1_EXT: &str = ".cmx";
pub const CF_V2_EXT: &str = ".cm";

/// Trait that defines functions for the retrieval of the bytes that define package definitions.
/// Used primarily to allow for nicer testing via mocking out the backing `fx serve` instance.
pub trait PackageReader: Send + Sync {
    /// Returns information for all packages served by an `fx serve` instance.
    fn read_targets(&self) -> Result<TargetsJson>;
    /// Takes a package name and a merkle hash and returns the package definition
    /// for the specified merkle hash. All valid CF files specified by the FAR
    /// archive pointed to by the merkle hash are parsed and returned.
    /// The package name is not validated.
    ///
    /// Currently only CFv1 is supported, CFv2 support is tracked here (fxb/53347).
    fn read_package_definition(&self, pkg_name: &str, merkle: &str) -> Result<PackageDefinition>;
    /// Reads the service package defined by the merkle hash.
    fn read_service_package_definition(&self, merkle: &str) -> Result<ServicePackageDefinition>;
    /// Reads the preset builtins definition file as a json blob.
    fn read_builtins(&self) -> Result<BuiltinsJson>;
}

pub struct PackageServerReader {
    fuchsia_root: String,
    pkg_getter: Box<dyn PackageGetter>,
}

impl PackageServerReader {
    pub fn new(fuchsia_root: String, pkg_getter: Box<dyn PackageGetter>) -> Self {
        Self { fuchsia_root: fuchsia_root, pkg_getter: pkg_getter }
    }

    fn read_blob_raw(&self, merkle: &str) -> Result<Vec<u8>> {
        Ok(self.pkg_getter.read_raw(&format!("blobs/{}", merkle)[..])?)
    }
}

impl PackageReader for PackageServerReader {
    fn read_targets(&self) -> Result<TargetsJson> {
        let resp_b = self.pkg_getter.read_raw("targets.json")?;
        let resp = str::from_utf8(&resp_b)?;

        Ok(serde_json::from_str(&resp)?)
    }

    fn read_package_definition(&self, pkg_name: &str, merkle: &str) -> Result<PackageDefinition> {
        // Retrieve the far archive from the package server.
        let resp_b = self.read_blob_raw(merkle)?;
        let mut cursor = Cursor::new(resp_b);
        let mut far = FarReader::new(&mut cursor)?;

        let mut pkg_def = PackageDefinition {
            url: util::to_package_url(pkg_name)?,
            typ: PackageType::Package,
            merkle: String::from(merkle), // FIXME: Do we need to copy? Or maybe we can just move it here?
            contents: HashMap::new(), // How do I do this better? Maybe I need to change PackageDefinition into a builder pattern
            cms: HashMap::new(),
        };

        // Find any parseable files from the archive.
        // Create a separate list of file names to read to avoid borrow checker
        // issues while iterating through the list.
        let mut cf_v1_files: Vec<String> = Vec::new();
        let mut cf_v2_files: Vec<String> = Vec::new();
        let mut contains_meta_contents = false;
        for item in far.list() {
            if item == "meta/contents" {
                contains_meta_contents = true;
            } else if item == "meta/package" {
                // TODO: Figure out if this is ever used
            } else {
                if item.starts_with("meta/") && item.ends_with(CF_V1_EXT) {
                    cf_v1_files.push(String::from(item));
                }
                if item.starts_with("meta/") && item.ends_with(CF_V2_EXT) {
                    cf_v2_files.push(String::from(item));
                }
            }
        }

        if contains_meta_contents {
            let content_b = far.read_file(&"meta/contents")?;
            let content_str = str::from_utf8(&content_b)?;
            pkg_def.contents = util::to_meta_contents_dict(content_str);
        }

        // Read the parseable files from the far archive and parse into json structs.
        for cmx_file in cf_v1_files {
            let item_b = far.read_file(&cmx_file)?;
            let item_json = str::from_utf8(&item_b)?;
            let cmx: CmxJson = serde_json::from_str(item_json)?;
            pkg_def.cms.insert(cmx_file, ComponentManifest::from(cmx));
        }

        // CV2 files are encoded with persistent FIDL and have a different
        // decoding structure.
        for cm_file in cf_v2_files {
            let decl_bytes = far.read_file(&cm_file)?;
            pkg_def.cms.insert(cm_file, ComponentManifest::from(decl_bytes));
        }
        Ok(pkg_def)
    }

    fn read_service_package_definition(&self, merkle: &str) -> Result<ServicePackageDefinition> {
        let cfg_pkg_b = self.read_blob_raw(&merkle)?;
        let mut cfg_pkg_str = str::from_utf8(&cfg_pkg_b)?;

        Ok(serde_json::from_str(&mut cfg_pkg_str)?)
    }

    fn read_builtins(&self) -> Result<BuiltinsJson> {
        let file = File::open(&format!("{}/scripts/scrutiny/builtins.json", self.fuchsia_root))?;
        let mut reader = BufReader::new(file);

        Ok(serde_json::from_reader(&mut reader)?)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_archive::write,
        std::collections::BTreeMap,
        std::io::{Cursor, Error, ErrorKind, Read},
        std::sync::RwLock,
    };

    struct MockPackageGetter {
        bytes: RwLock<Vec<Vec<u8>>>,
    }

    impl MockPackageGetter {
        fn new() -> Self {
            Self { bytes: RwLock::new(Vec::new()) }
        }

        fn append_bytes(&self, byte_vec: Vec<u8>) {
            self.bytes.write().unwrap().push(byte_vec);
        }
    }

    impl PackageGetter for MockPackageGetter {
        fn read_raw(&self, _path: &str) -> std::io::Result<Vec<u8>> {
            let mut borrow = self.bytes.write().unwrap();
            {
                if borrow.len() == 0 {
                    return Err(Error::new(
                        ErrorKind::Other,
                        "No more byte vectors left to return. Maybe append more?",
                    ));
                }
                Ok(borrow.remove(0))
            }
        }
    }

    #[test]
    fn read_package_definition_ignores_invalid_files() {
        let contents_b = "a=b\nc=d\n1=2".as_bytes();
        let foo_str = serde_json::json!({
            "sandbox": {
                "services": ["service_one", "service_two"],
            },
        })
        .to_string();
        let foo_b = foo_str.as_bytes();
        let bar_str = serde_json::json!({
            "sandbox": {
                "services": ["aries", "taurus"],
            },
        })
        .to_string();
        let bar_b = bar_str.as_bytes();
        let baz_b = "baz\n".as_bytes();
        let mut path_content_map: BTreeMap<&str, (u64, Box<dyn Read>)> = BTreeMap::new();
        path_content_map.insert("meta/contents", (contents_b.len() as u64, Box::new(contents_b)));
        path_content_map.insert("meta/foo.cmx", (foo_b.len() as u64, Box::new(foo_b)));
        path_content_map.insert("meta/bar.cmx", (bar_b.len() as u64, Box::new(bar_b)));
        path_content_map.insert("meta/baz", (baz_b.len() as u64, Box::new(baz_b)));
        path_content_map.insert("grr.cmx", (baz_b.len() as u64, Box::new(baz_b)));
        let mut target = Cursor::new(Vec::new());
        write(&mut target, path_content_map).unwrap();

        let mock_getter = MockPackageGetter::new();
        mock_getter.append_bytes(target.into_inner());

        let pkg_reader = PackageServerReader::new(String::from("/"), Box::new(mock_getter));
        let result = pkg_reader.read_package_definition("foo", "bar").unwrap();
        assert_eq!(result.contents.len(), 3);
        assert_eq!(result.contents["a"], "b");
        assert_eq!(result.contents["c"], "d");
        assert_eq!(result.contents["1"], "2");
        assert_eq!(result.cms.len(), 2);
        if let ComponentManifest::Version1(sb) = &result.cms["meta/foo.cmx"] {
            if let Some(services) = &sb.services {
                assert_eq!(services[0], "service_one");
                assert_eq!(services[1], "service_two");
            } else {
                panic!("Expected services to be Some()");
            }
        } else {
            panic!("Expected results sandbox to be Some()");
        }

        if let ComponentManifest::Version1(sb) = &result.cms["meta/bar.cmx"] {
            if let Some(services) = &sb.services {
                assert_eq!(services[0], "aries");
                assert_eq!(services[1], "taurus");
            } else {
                panic!("Expected services to be Some()");
            }
        } else {
            panic!("Expected results sandbox to be Some()");
        }
    }
}
