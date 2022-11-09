// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::{
        package::{is_cf_v1_manifest, is_cf_v2_config_values, is_cf_v2_manifest},
        util::{
            jsons::{CmxJson, ServicePackageDefinition},
            types::{ComponentManifest, PackageDefinition, PartialPackageDefinition},
        },
    },
    anyhow::{anyhow, Context, Result},
    fuchsia_archive::Utf8Reader as FarReader,
    fuchsia_hash::Hash,
    fuchsia_url::AbsolutePackageUrl,
    scrutiny_utils::{
        artifact::ArtifactReader,
        io::ReadSeek,
        key_value::parse_key_value,
        package::{open_update_package, read_content_blob, META_CONTENTS_PATH},
    },
    serde_json::Value,
    std::{
        collections::HashSet,
        ffi::OsStr,
        fs::File,
        path::{Path, PathBuf},
        str::{self, FromStr},
    },
    update_package::parse_packages_json,
};

/// Trait that defines functions for the retrieval of the bytes that define package definitions.
/// Used primarily to allow for nicer testing via mocking out the backing `fx serve` instance.
pub trait PackageReader: Send + Sync {
    /// Returns the fully-qualified URLs of packages expected to be reachable
    /// via this reader.
    fn read_package_urls(&mut self) -> Result<Vec<AbsolutePackageUrl>>;
    /// Takes a package name and a merkle hash and returns the package definition
    /// for the specified merkle hash. All valid CF files specified by the FAR
    /// archive pointed to by the merkle hash are parsed and returned.
    /// The package name is not validated.
    ///
    /// Currently only CFv1 is supported, CFv2 support is tracked here (fxbug.dev/53347).
    fn read_package_definition(
        &mut self,
        pkg_url: &AbsolutePackageUrl,
    ) -> Result<PackageDefinition>;
    /// Identical to `read_package_definition`, except read the update package bound to this reader.
    /// If reader is not bound to an update package or an error occurs reading the update package,
    /// return an `Err(...)`.
    fn read_update_package_definition(&mut self) -> Result<PartialPackageDefinition>;
    /// Reads the service package from the provided data.
    fn read_service_package_definition(&mut self, data: &[u8]) -> Result<ServicePackageDefinition>;
    /// Gets the paths to files touched by read operations.
    fn get_deps(&self) -> HashSet<PathBuf>;
}

pub struct PackagesFromUpdateReader {
    update_package_path: PathBuf,
    blob_reader: Box<dyn ArtifactReader>,
    deps: HashSet<PathBuf>,
}

impl PackagesFromUpdateReader {
    pub fn new<P: AsRef<Path>>(
        update_package_path: P,
        blob_reader: Box<dyn ArtifactReader>,
    ) -> Self {
        Self {
            update_package_path: update_package_path.as_ref().to_path_buf(),
            blob_reader,
            deps: HashSet::new(),
        }
    }
}

impl PackageReader for PackagesFromUpdateReader {
    fn read_package_urls(&mut self) -> Result<Vec<AbsolutePackageUrl>> {
        self.deps.insert(self.update_package_path.clone());
        let mut far_reader = open_update_package(&self.update_package_path, &mut self.blob_reader)
            .context("Failed to open update meta.far for package reader")?;
        let packages_json_contents =
            read_content_blob(&mut far_reader, &mut self.blob_reader, "packages.json")
                .context("Failed to open update packages.json in update meta.far")?;
        tracing::info!(
            "packages.json contents: \"\"\"\n{}\n\"\"\"",
            std::str::from_utf8(packages_json_contents.as_slice()).unwrap()
        );
        parse_packages_json(packages_json_contents.as_slice())
            .context("Failed to parse packages.json file from update package")
    }

    fn read_package_definition(
        &mut self,
        pkg_url: &AbsolutePackageUrl,
    ) -> Result<PackageDefinition> {
        let pkg_hash = pkg_url
            .hash()
            .ok_or_else(|| anyhow!("Cannot read package definition without package hash"))?;
        let meta_far = self
            .blob_reader
            .open(&Path::new(&format!("{}", pkg_hash)))
            .with_context(|| format!("Failed to open meta.far blob for package {}", pkg_url))?;
        read_package_definition(pkg_url, meta_far)
    }

    fn read_update_package_definition(&mut self) -> Result<PartialPackageDefinition> {
        self.deps.insert(self.update_package_path.clone());
        let update_package = File::open(&self.update_package_path).with_context(|| {
            format!("Failed to open update package: {:?}", self.update_package_path)
        })?;
        read_partial_package_definition(update_package)
    }

    /// Reads the raw config-data package definition and converts it into a
    /// ServicePackageDefinition.
    fn read_service_package_definition(&mut self, data: &[u8]) -> Result<ServicePackageDefinition> {
        read_service_package_definition(data)
    }

    fn get_deps(&self) -> HashSet<PathBuf> {
        self.deps.union(&self.blob_reader.get_deps()).map(PathBuf::clone).collect()
    }
}

pub fn read_package_definition(
    pkg_url: &AbsolutePackageUrl,
    data: impl ReadSeek,
) -> Result<PackageDefinition> {
    let partial = read_partial_package_definition(data)
        .with_context(|| format!("Failed to construct package definition for {:?}", pkg_url))?;
    Ok(PackageDefinition::new(pkg_url.clone(), partial))
}

pub fn read_partial_package_definition(rs: impl ReadSeek) -> Result<PartialPackageDefinition> {
    let mut far_reader =
        FarReader::new(rs).context("Failed to construct meta.far reader for package")?;

    let mut pkg_def = PartialPackageDefinition::default();

    // Find any parseable files from the archive.
    // Create a separate list of file names to read to avoid borrow checker
    // issues while iterating through the list.
    let mut cf_v1_files = Vec::<String>::new();
    let mut cf_v2_files = Vec::<String>::new();
    let mut cf_v2_config_files = Vec::<String>::new();
    let mut contains_meta_contents = false;
    for item in far_reader.list().map(|e| e.path()) {
        if item == META_CONTENTS_PATH {
            contains_meta_contents = true;
        } else {
            let path_buf: PathBuf = OsStr::new(item).into();
            if is_cf_v1_manifest(&path_buf) {
                cf_v1_files.push(item.to_string());
            } else if is_cf_v2_manifest(&path_buf) {
                cf_v2_files.push(item.to_string());
            } else if is_cf_v2_config_values(&path_buf) {
                cf_v2_config_files.push(item.to_string());
            }
        }
    }

    // Meta files exist directly in the FAR and aren't represented by blobs.
    // Create a separate list of file names to read to avoid borrow checker
    // issues while iterating through the list.
    let meta_paths: Vec<String> = far_reader.list().map(|item| item.path().to_string()).collect();
    for meta_path in meta_paths.iter() {
        let meta_bytes = far_reader.read_file(meta_path).with_context(|| {
            format!("Failed to read file {} from meta.far for package", meta_path)
        })?;
        pkg_def.meta.insert(meta_path.into(), meta_bytes);
    }

    if contains_meta_contents {
        let content_bytes = far_reader
            .read_file(&META_CONTENTS_PATH)
            .context("Failed to read file meta/contents from meta.far for package")?;
        let content_str = str::from_utf8(&content_bytes)
            .context("Failed decode file meta/contents as UTF8-encoded string")?;
        pkg_def.contents = parse_key_value(content_str).with_context(|| {
            format!("Failed to parse file meta/contents for package as path=merkle pairs; file contents:\n\"\"\"\n{}\n\"\"\"", content_str)
        })?
        .into_iter()
        .map(|(key, value)| {
            let (path, hash) = (PathBuf::from(key), Hash::from_str(&value)?);
            Ok((path, hash))
        }).collect::<Result<Vec<(PathBuf, Hash)>>>().with_context(|| {
            format!("Failed to parse (path,hash) pairs from file meta/contents for package as path=merkle pairs; file contents:\n\"\"\"\n{}\n\"\"\"", content_str)
        })?
        .into_iter()
        .collect();
    }

    // Read the parseable files from the far archive and parse into json structs.
    for cmx_file in cf_v1_files.iter() {
        let item_bytes = far_reader.read_file(cmx_file).with_context(|| {
            format!("Failed to read file {} from meta.far for package", cmx_file)
        })?;
        let item_json = str::from_utf8(&item_bytes)
            .with_context(|| format!("Failed decode file {} as UTF8-encoded string", cmx_file))?;
        let cmx: CmxJson = serde_json::from_str(item_json).with_context(|| {
            format!("Failed decode file {} for package as JSON/CMX string", cmx_file)
        })?;
        pkg_def.cms.insert(cmx_file.into(), ComponentManifest::from(cmx));
    }

    // CV2 files are encoded with persistent FIDL and have a different
    // decoding structure.
    for cm_file in cf_v2_files.iter() {
        let decl_bytes = far_reader.read_file(cm_file).with_context(|| {
            format!("Failed to read file {} from meta.far for package", cm_file)
        })?;
        pkg_def.cms.insert(cm_file.into(), ComponentManifest::from(decl_bytes));
    }

    for cvf_file in &cf_v2_config_files {
        let values_bytes = far_reader.read_file(cvf_file).with_context(|| {
            format!("Failed to read file {} from meta.far for package", cvf_file)
        })?;
        pkg_def.cvfs.insert(cvf_file.into(), values_bytes);
    }

    Ok(pkg_def)
}

pub fn read_service_package_definition(data: &[u8]) -> Result<ServicePackageDefinition> {
    let json: Value = serde_json::from_slice(data)
        .context("Failed to parse service package definition as JSON string")?;
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
                        apps.push(String::from(inner[0].as_str().unwrap()).into());
                    }
                }
            }
            service_def.apps = Some(apps);
        }
    }
    Ok(service_def)
}

#[cfg(test)]
mod tests {
    use {
        super::{PackageReader, PackagesFromUpdateReader},
        crate::core::util::types::ComponentManifest,
        fuchsia_archive::write,
        fuchsia_merkle::MerkleTree,
        fuchsia_url::{AbsolutePackageUrl, PackageName, PackageVariant},
        scrutiny_testing::{artifact::MockArtifactReader, TEST_REPO_URL},
        scrutiny_utils::package::META_CONTENTS_PATH,
        std::collections::BTreeMap,
        std::{
            fs::File,
            io::{Cursor, Read},
            path::{Path, PathBuf},
            str::FromStr,
        },
        tempfile::tempdir,
        update_package::serialize_packages_json,
    };

    fn fake_update_package<P: AsRef<Path>>(
        path: P,
        pkg_urls: &[AbsolutePackageUrl],
    ) -> MockArtifactReader {
        let packages_json_contents = serialize_packages_json(pkg_urls).unwrap();
        let packages_json_merkle =
            MerkleTree::from_reader(packages_json_contents.as_slice()).unwrap().root();
        let meta_contents_string = format!("packages.json={}\n", packages_json_merkle);
        let meta_contents_str = &meta_contents_string;
        let meta_contents_bytes = meta_contents_str.as_bytes();
        let mut path_content_map: BTreeMap<&str, (u64, Box<dyn Read>)> = BTreeMap::new();
        path_content_map.insert(
            META_CONTENTS_PATH,
            (meta_contents_bytes.len() as u64, Box::new(meta_contents_bytes)),
        );
        let mut update_pkg = File::create(path).unwrap();
        write(&mut update_pkg, path_content_map).unwrap();

        let mut mock_artifact_reader = MockArtifactReader::new();
        mock_artifact_reader
            .append_artifact(&packages_json_merkle.to_string(), packages_json_contents);
        mock_artifact_reader
    }

    #[fuchsia::test]
    fn read_package_service_definition() {
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

        let temp_dir = tempdir().unwrap();
        let update_pkg_path = temp_dir.path().join("update.far");

        // This test does not need to process packages designated in its update package. Simply
        // create a well-formed-enough update package to satisfy construction of a
        // PackagesFromUpdateReader.
        let mock_artifact_reader = fake_update_package(&update_pkg_path, vec![].as_slice());

        let mut pkg_reader =
            PackagesFromUpdateReader::new(&update_pkg_path, Box::new(mock_artifact_reader));
        let result = pkg_reader.read_service_package_definition(service_def.as_bytes()).unwrap();
        assert_eq!(
            result.apps.unwrap(),
            vec![
                "fuchsia-pkg://fuchsia.com/foo#meta/foo.cmx".to_string(),
                "fuchsia-pkg://fuchsia.com/bar#meta/bar.cmx".to_string()
            ]
        );
        assert_eq!(result.services.unwrap().len(), 2);
    }

    #[fuchsia::test]
    fn read_package_definition_ignores_invalid_files() {
        // `meta/foo.cmx`: Valid service definitions "service_one" and "service_two".
        let foo_string = serde_json::json!({
            "sandbox": {
                "services": ["service_one", "service_two"],
            },
        })
        .to_string();
        let foo_bytes = foo_string.as_bytes();

        // `meta/bar.cmx`: Valid service definitions "aries" and "taurus".
        let bar_string = serde_json::json!({
            "sandbox": {
                "services": ["aries", "taurus"],
            },
        })
        .to_string();
        let bar_bytes = bar_string.as_bytes();

        // `meta/baz` and `grr.cmx` with the same content: not valid service definitions.
        let baz_bytes = "baz\n".as_bytes();

        // Reuse paths "a", "c", and "1" as content of non-meta test files.
        let a_str = "a";
        let c_str = "c";
        let one_str = "1";
        let a_path = PathBuf::from(a_str);
        let c_path = PathBuf::from(c_str);
        let one_path = PathBuf::from(one_str);
        let a_hash = MerkleTree::from_reader(a_str.as_bytes()).unwrap().root();
        let c_hash = MerkleTree::from_reader(c_str.as_bytes()).unwrap().root();
        let one_hash = MerkleTree::from_reader(one_str.as_bytes()).unwrap().root();
        let meta_contents_string =
            format!("{}={}\n{}={}\n{}={}\n", a_str, a_hash, c_str, c_hash, one_str, one_hash);
        let meta_contents_str = &meta_contents_string;
        let meta_contents_bytes = meta_contents_str.as_bytes();

        let mut path_content_map: BTreeMap<&str, (u64, Box<dyn Read>)> = BTreeMap::new();
        path_content_map.insert(
            META_CONTENTS_PATH,
            (meta_contents_bytes.len() as u64, Box::new(meta_contents_bytes)),
        );

        let meta_foo_cmx_str = "meta/foo.cmx";
        let meta_bar_cmx_str = "meta/bar.cmx";
        let meta_baz_str = "meta/baz";
        let grr_cmx_str = "meta.cmx";
        let meta_foo_cmx_path = PathBuf::from(meta_foo_cmx_str);
        let meta_bar_cmx_path = PathBuf::from(meta_bar_cmx_str);
        // No expectations require construction of a `meta_baz_path` or `grr_cmx_path`.
        path_content_map.insert(meta_foo_cmx_str, (foo_bytes.len() as u64, Box::new(foo_bytes)));
        path_content_map.insert(meta_bar_cmx_str, (bar_bytes.len() as u64, Box::new(bar_bytes)));
        path_content_map.insert(meta_baz_str, (baz_bytes.len() as u64, Box::new(baz_bytes)));
        path_content_map.insert(grr_cmx_str, (baz_bytes.len() as u64, Box::new(baz_bytes)));

        // Construct package named `foo`.
        let mut target = Cursor::new(Vec::new());
        write(&mut target, path_content_map).unwrap();
        let pkg_contents = target.get_ref();
        let pkg_merkle = MerkleTree::from_reader(pkg_contents.as_slice()).unwrap().root();
        let pkg_url = AbsolutePackageUrl::new(
            TEST_REPO_URL.clone(),
            PackageName::from_str("foo").unwrap(),
            Some(PackageVariant::zero()),
            Some(pkg_merkle),
        );

        // Fake update package designates `foo` package defined above.
        let temp_dir = tempdir().unwrap();
        let update_pkg_path = temp_dir.path().join("update.far");
        let mut mock_artifact_reader =
            fake_update_package(&update_pkg_path, vec![pkg_url.clone()].as_slice());

        // Add all artifacts to the test artifact reader.
        mock_artifact_reader.append_artifact(&a_hash.to_string(), Vec::from(a_str.as_bytes()));
        mock_artifact_reader.append_artifact(&c_hash.to_string(), Vec::from(c_str.as_bytes()));
        mock_artifact_reader.append_artifact(&one_hash.to_string(), Vec::from(one_str.as_bytes()));
        mock_artifact_reader.append_artifact(&pkg_merkle.to_string(), pkg_contents.clone());

        let mut pkg_reader =
            PackagesFromUpdateReader::new(&update_pkg_path, Box::new(mock_artifact_reader));

        let result = pkg_reader.read_package_definition(&pkg_url).unwrap();
        assert_eq!(result.contents.len(), 3);
        assert_eq!(result.contents[&a_path], a_hash);
        assert_eq!(result.contents[&c_path], c_hash);
        assert_eq!(result.contents[&one_path], one_hash);
        assert_eq!(result.cms.len(), 2);
        if let ComponentManifest::Version1(sb) = &result.cms[&meta_foo_cmx_path] {
            if let Some(services) = &sb.services {
                assert_eq!(services[0], "service_one");
                assert_eq!(services[1], "service_two");
            } else {
                panic!("Expected services to be Some()");
            }
        } else {
            panic!("Expected results sandbox to be Some()");
        }

        if let ComponentManifest::Version1(sb) = &result.cms[&meta_bar_cmx_path] {
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
