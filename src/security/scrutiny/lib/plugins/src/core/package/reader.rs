// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::{package::getter::*, util, util::jsons::*, util::types::*},
    anyhow::Result,
    fuchsia_archive::Reader as FarReader,
    serde_json::Value,
    std::{
        collections::{HashMap, HashSet},
        io::Cursor,
        str,
    },
};

// Constants/Statics
pub const CF_V1_EXT: &str = ".cmx";
pub const CF_V2_EXT: &str = ".cm";

/// Trait that defines functions for the retrieval of the bytes that define package definitions.
/// Used primarily to allow for nicer testing via mocking out the backing `fx serve` instance.
pub trait PackageReader: Send + Sync {
    /// Returns information for all packages served by an `fx serve` instance.
    fn read_targets(&mut self) -> Result<TargetsJson>;
    /// Takes a package name and a merkle hash and returns the package definition
    /// for the specified merkle hash. All valid CF files specified by the FAR
    /// archive pointed to by the merkle hash are parsed and returned.
    /// The package name is not validated.
    ///
    /// Currently only CFv1 is supported, CFv2 support is tracked here (fxbug.dev/53347).
    fn read_package_definition(
        &mut self,
        pkg_name: &str,
        merkle: &str,
    ) -> Result<PackageDefinition>;
    /// Reads the service package from the provided data.
    fn read_service_package_definition(&mut self, data: String)
        -> Result<ServicePackageDefinition>;
    /// Gets the paths to files touched by read operations.
    fn get_deps(&self) -> HashSet<String>;
}

pub struct PackageServerReader {
    pkg_getter: Box<dyn PackageGetter>,
}

impl PackageServerReader {
    pub fn new(pkg_getter: Box<dyn PackageGetter>) -> Self {
        Self { pkg_getter }
    }

    fn read_blob_raw(&mut self, merkle: &str) -> Result<Vec<u8>> {
        Ok(self.pkg_getter.read_raw(&format!("blobs/{}", merkle)[..])?)
    }
}

impl PackageReader for PackageServerReader {
    fn read_targets(&mut self) -> Result<TargetsJson> {
        let resp_b = self.pkg_getter.read_raw("targets.json")?;
        let resp = str::from_utf8(&resp_b)?;

        Ok(serde_json::from_str(&resp)?)
    }

    fn read_package_definition(
        &mut self,
        pkg_name: &str,
        merkle: &str,
    ) -> Result<PackageDefinition> {
        // Retrieve the far archive from the package server.
        let resp_b = self.read_blob_raw(merkle)?;
        let mut cursor = Cursor::new(resp_b);
        let mut far = FarReader::new(&mut cursor)?;

        let mut pkg_def = PackageDefinition {
            url: util::to_package_url(pkg_name)?,
            merkle: String::from(merkle), // FIXME: Do we need to copy? Or maybe we can just move it here?
            meta: HashMap::new(),
            contents: HashMap::new(), // How do I do this better? Maybe I need to change PackageDefinition into a builder pattern
            cms: HashMap::new(),
        };

        // Find any parseable files from the archive.
        // Create a separate list of file names to read to avoid borrow checker
        // issues while iterating through the list.
        let mut cf_v1_files: Vec<String> = Vec::new();
        let mut cf_v2_files: Vec<String> = Vec::new();
        let mut contains_meta_contents = false;
        for item in far.list().map(|e| e.path()) {
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

        // Meta files exist directly in the FAR and aren't represented by blobs.
        let meta_files: Vec<String> = far.list().map(|e| e.path().to_string()).collect();
        for meta_item in meta_files {
            let meta_bytes = far.read_file(&meta_item)?;
            if let Ok(meta_contents) = str::from_utf8(&meta_bytes) {
                pkg_def.meta.insert(String::from(meta_item), meta_contents.to_string());
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

    /// Reads the raw config-data package definition and converts it into a
    /// ServicePackageDefinition.
    fn read_service_package_definition(
        &mut self,
        data: String,
    ) -> Result<ServicePackageDefinition> {
        let json: Value = serde_json::from_str(&data)?;
        let mut service_def = ServicePackageDefinition { services: None, apps: None };
        if let Some(json_services) = json.get("services") {
            service_def.services = Some(serde_json::from_value(json_services.clone()).unwrap());
        }
        // App entries can be a composite of strings and vectors which serde doesn't handle
        // by default. So this checks each entry for its underlying type and flattens the
        // structure to extract just the app(the first entry in the array) or the
        // string if it is not part of an inner array.
        if let Some(json_apps) = json.get("apps") {
            if json_apps.is_array() {
                let mut apps = Vec::new();
                for e in json_apps.as_array().unwrap() {
                    if e.is_string() {
                        apps.push(String::from(e.as_str().unwrap()));
                    } else if e.is_array() {
                        let inner = e.as_array().unwrap();
                        if inner.len() > 0 {
                            apps.push(String::from(inner[0].as_str().unwrap()));
                        }
                    }
                }
                service_def.apps = Some(apps);
            }
        }
        Ok(service_def)
    }

    fn get_deps(&self) -> HashSet<String> {
        self.pkg_getter.get_deps()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::core::package::test_utils::MockPackageGetter,
        fuchsia_archive::write,
        std::collections::BTreeMap,
        std::io::{Cursor, Read},
    };

    #[test]
    fn read_package_service_definition() {
        let mock_getter = MockPackageGetter::new();

        let service_def = "{
            \"apps\": [
                       \"fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx\",
                       [\"fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx\", \"--test\"]
            ],
            \"services\": {
                \"fuchsia.foo.Foo\": \"fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx\",
                \"fuchsia.bar.Bar\": \"fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx\"
            }
        }";
        let mut pkg_reader = PackageServerReader::new(Box::new(mock_getter));
        let result = pkg_reader.read_service_package_definition(service_def.to_string()).unwrap();
        assert_eq!(
            result.apps.unwrap(),
            vec![
                "fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx".to_string(),
                "fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx".to_string()
            ]
        );
        assert_eq!(result.services.unwrap().len(), 2);
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

        let mut pkg_reader = PackageServerReader::new(Box::new(mock_getter));
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
