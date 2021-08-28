// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Result},
    log::warn,
    serde::Deserialize,
    serde_json::Value,
    std::{
        collections::HashMap,
        convert::Into,
        fs,
        io::{self, BufReader},
        path::PathBuf,
    },
};

#[derive(Debug, PartialEq, Eq)]
pub enum SdkVersion {
    Version(String),
    InTree,
    Unknown,
}

pub struct Sdk {
    path_prefix: PathBuf,
    metas: Vec<Value>,
    real_paths: Option<HashMap<String, String>>,
    version: SdkVersion,
}

#[derive(Deserialize)]
struct SdkAtoms {
    #[cfg(test)]
    ids: Vec<Value>,
    atoms: Vec<Atom>,
}

#[derive(Deserialize)]
struct Atom {
    #[cfg(test)]
    category: String,
    #[cfg(test)]
    deps: Vec<String>,
    files: Vec<File>,
    #[serde(rename = "gn-label")]
    #[cfg(test)]
    gn_label: String,
    #[cfg(test)]
    id: String,
    meta: String,
    #[serde(rename = "type")]
    #[cfg(test)]
    ty: String,
}

#[derive(Deserialize)]
struct File {
    destination: String,
    source: String,
}

#[derive(Deserialize)]
struct Manifest {
    #[allow(unused)]
    arch: Value,
    id: Option<String>,
    parts: Vec<Part>,
    #[allow(unused)]
    schema_version: String,
}

#[derive(Deserialize)]
struct Part {
    meta: String,
    #[serde(rename = "type")]
    #[allow(unused)]
    ty: String,
}

impl Sdk {
    pub fn from_build_dir(path: PathBuf) -> Result<Self> {
        let mut manifest_path = path.clone();
        let file = if cfg!(target_arch = "x86_64") {
            manifest_path.push("host_x64/sdk/manifest/host_tools.modular");
            fs::File::open(&manifest_path).map_err(Into::into)
        } else if cfg!(target_arch = "aarch64") {
            manifest_path.push("host_arm64/sdk/manifest/host_tools.modular");
            fs::File::open(&manifest_path).map_err(Into::into)
        } else {
            Err(anyhow!("Host architecture not supported"))
        }
        .context(format!("opening sdk path: {:?}", path));

        let file = match file {
            Ok(file) => file,
            Err(e) => {
                manifest_path = path.join("sdk/manifest/core");
                fs::File::open(&manifest_path).map_err(|_| e)?
            }
        };

        // If we are able to parse the json file into atoms, creates a Sdk object from the atoms.
        Self::from_sdk_atoms(
            path,
            Self::atoms_from_core_manifest(manifest_path, BufReader::new(file))?,
            Self::open_meta,
            SdkVersion::InTree,
        )
    }

    fn atoms_from_core_manifest<T>(manifest_path: PathBuf, reader: BufReader<T>) -> Result<SdkAtoms>
    where
        T: io::Read,
    {
        let atoms: serde_json::Result<SdkAtoms> = serde_json::from_reader(reader);

        match atoms {
            Ok(result) => Ok(result),
            Err(e) => Err(anyhow!("Can't read json file {:?}: {:?}", manifest_path, e)),
        }
    }

    pub fn from_sdk_dir(path_prefix: PathBuf) -> Result<Self> {
        let manifest_path = path_prefix.join("meta/manifest.json");
        let mut version = SdkVersion::Unknown;

        Self::metas_from_sdk_manifest(
            BufReader::new(
                fs::File::open(manifest_path.clone())
                    .context(format!("opening sdk manifest path: {:?}", manifest_path))?,
            ),
            &mut version,
            |meta| {
                let meta_path = path_prefix.join(meta);
                fs::File::open(meta_path.clone())
                    .context(format!("opening sdk path: {:?}", meta_path))
                    .ok()
                    .map(BufReader::new)
            },
        )
        .map(|metas| Sdk { path_prefix, metas, real_paths: None, version })
    }

    fn metas_from_sdk_manifest<M, T>(
        manifest: BufReader<T>,
        version: &mut SdkVersion,
        get_meta: M,
    ) -> Result<Vec<Value>>
    where
        M: Fn(&str) -> Option<BufReader<T>>,
        T: io::Read,
    {
        let manifest: Manifest = serde_json::from_reader(manifest)?;
        // TODO: Check the schema version and log a warning if it's not what we expect.

        if let Some(id) = manifest.id {
            *version = SdkVersion::Version(id.clone());
        }

        let metas = manifest
            .parts
            .into_iter()
            .map(|x| match get_meta(&x.meta) {
                Some(reader) => serde_json::from_reader(reader).map_err(|x| x.into()),
                None => serde_json::from_str("{}").map_err(|x| x.into()),
            })
            .collect::<Result<Vec<_>>>()?;
        Ok(metas)
    }

    pub fn get_host_tool(&self, name: &str) -> Result<PathBuf> {
        match self.get_host_tool_relative_path(name) {
            Ok(path) => {
                let result = self.path_prefix.join(path);
                Ok(result)
            }
            Err(error) => Err(error),
        }
    }

    fn get_host_tool_relative_path(&self, name: &str) -> Result<PathBuf> {
        self.get_real_path(
            self.metas
                .iter()
                .filter(|x| {
                    x.get("name")
                        .filter(|n| *n == name)
                        .and(x.get("type").filter(|t| *t == "host_tool"))
                        .is_some()
                })
                .map(|x| -> Result<_> {
                    let arr = x
                        .get("files")
                        .ok_or(anyhow!(
                            "No executable provided for tool '{}' (no file list present)",
                            name
                        ))?
                        .as_array()
                        .ok_or(anyhow!(
                            "Malformed manifest for tool '{}': file list wasn't an array",
                            name
                        ))?;

                    if arr.len() > 1 {
                        warn!("Tool '{}' provides multiple files in manifest", name);
                    }

                    arr.get(0)
                        .ok_or(anyhow!(
                            "No executable provided for tool '{}' (file list was empty)",
                            name
                        ))?
                        .as_str()
                        .ok_or(anyhow!(
                            "Malformed manifest for tool '{}': file name wasn't a string",
                            name
                        ))
                })
                .collect::<Result<Vec<_>>>()?
                .into_iter()
                .min_by_key(|x| x.len()) // Shortest path is the one with no arch specifier, i.e. the default arch, i.e. the current arch (we hope.)
                .ok_or(anyhow!("Tool '{}' not found", name))?,
        )
    }

    fn get_real_path(&self, path: impl AsRef<str>) -> Result<PathBuf> {
        match &self.real_paths {
            Some(map) => map.get(path.as_ref()).map(PathBuf::from).ok_or(anyhow!(
                "SDK File '{}' has no source in the build directory",
                path.as_ref()
            )),
            _ => Ok(PathBuf::from(path.as_ref())),
        }
    }

    pub fn get_path_prefix(&self) -> &PathBuf {
        &self.path_prefix
    }

    pub fn get_version(&self) -> &SdkVersion {
        &self.version
    }

    /// For tests only
    #[doc(hidden)]
    pub fn get_empty_sdk_with_version(version: SdkVersion) -> Self {
        Sdk { path_prefix: PathBuf::new(), metas: Vec::new(), real_paths: None, version }
    }

    /// Opens a meta file with the given path. Returns a buffered reader.
    fn open_meta(file_path: &PathBuf) -> Result<BufReader<fs::File>> {
        let file = fs::File::open(&file_path);
        match file {
            Ok(file) => Ok(BufReader::new(file)),
            Err(error) => return Err(anyhow!("Can't open {:?}: {:?}", file_path, error)),
        }
    }

    /// Allocates a new Sdk using the given atoms.
    ///
    /// All the meta files specified in the atoms are loaded.
    /// The creation succeed only if all the meta files have been loaded successfully.
    fn from_sdk_atoms<T, U>(
        path_prefix: PathBuf,
        atoms: SdkAtoms,
        get_meta: T,
        version: SdkVersion,
    ) -> Result<Self>
    where
        T: Fn(&PathBuf) -> Result<BufReader<U>>,
        U: io::Read,
    {
        let mut metas = Vec::new();
        let mut real_paths = HashMap::new();

        for atom in atoms.atoms.iter() {
            for file in atom.files.iter() {
                real_paths.insert(file.destination.clone(), file.source.clone());
            }

            let meta_file_name = real_paths
                .get(&atom.meta)
                .ok_or(anyhow!("Atom did not specify source for its metadata."))?;
            let full_meta_path = path_prefix.join(meta_file_name);

            let reader = get_meta(&full_meta_path);
            let json_metas = serde_json::from_reader(reader?);

            match json_metas {
                Ok(result) => metas.push(result),
                Err(e) => {
                    return Err(anyhow!("Can't read json file {:?}: {:?}", full_meta_path, e))
                }
            }
        }

        Ok(Sdk { path_prefix, metas, real_paths: Some(real_paths), version })
    }
}

////////////////////////////////////////////////////////////////////////////////
// tests

#[cfg(test)]
mod test {
    use super::*;

    const CORE_MANIFEST: &str = r#"{
      "atoms": [
        {
          "category": "partner",
          "deps": [],
          "files": [
            {
              "destination": "device/generic-arm64.json",
              "source": "gen/sdk/devices/generic-arm64.meta.json"
            }
          ],
          "gn-label": "//sdk/devices:generic-arm64(//build/toolchain/fuchsia:x64)",
          "id": "sdk://device/generic-arm64",
          "meta": "device/generic-arm64.json",
          "type": "device_profile"
        },
        {
          "category": "partner",
          "deps": [],
          "files": [
            {
              "destination": "tools/x64/zxdb",
              "source": "host_x64/zxdb"
            },
            {
              "destination": "tools/x64/zxdb-meta.json",
              "source": "host_x64/gen/src/developer/debug/zxdb/zxdb_sdk.meta.json"
            }
          ],
          "gn-label": "//src/developer/debug/zxdb:zxdb_sdk(//build/toolchain:host_x64)",
          "id": "sdk://tools/x64/zxdb",
          "meta": "tools/x64/zxdb-meta.json",
          "type": "host_tool"
        },
        {
          "category": "partner",
          "deps": [],
          "files": [
            {
              "destination": "tools/arm64/symbol-index",
              "source": "host_arm64/symbol-index"
            },
            {
              "destination": "tools/arm64/symbol-index-meta.json",
              "source": "host_arm64/gen/tools/symbol-index/symbol_index_sdk.meta.json"
            }
          ],
          "gn-label": "//tools/symbol-index:symbol_index_sdk(//build/toolchain:host_arm64)",
          "id": "sdk://tools/arm64/symbol-index",
          "meta": "tools/arm64/symbol-index-meta.json",
          "type": "host_tool"
        },
        {
          "category": "partner",
          "deps": [],
          "files": [
            {
              "destination": "tools/symbol-index",
              "source": "host_x64/symbol-index"
            },
            {
              "destination": "tools/symbol-index-meta.json",
              "source": "host_x64/gen/tools/symbol-index/symbol_index_sdk_legacy.meta.json"
            }
          ],
          "gn-label": "//tools/symbol-index:symbol_index_sdk_legacy(//build/toolchain:host_x64)",
          "id": "sdk://tools/symbol-index",
          "meta": "tools/symbol-index-meta.json",
          "type": "host_tool"
        },
        {
          "category": "partner",
          "deps": [],
          "files": [
            {
              "destination": "tools/x64/symbol-index",
              "source": "host_x64/symbol-index"
            },
            {
              "destination": "tools/x64/symbol-index-meta.json",
              "source": "host_x64/gen/tools/symbol-index/symbol_index_sdk.meta.json"
            }
          ],
          "gn-label": "//tools/symbol-index:symbol_index_sdk(//build/toolchain:host_x64)",
          "id": "sdk://tools/x64/symbol-index",
          "meta": "tools/x64/symbol-index-meta.json",
          "type": "host_tool"
        }
      ],
      "ids": []
    }"#;

    fn get_core_manifest_meta(file_path: &PathBuf) -> Result<BufReader<&'static [u8]>> {
        let name = file_path.to_str().unwrap();
        if name == "/fuchsia/out/default/gen/sdk/devices/generic-arm64.meta.json" {
            const META: &str = r#"{
              "description": "A generic arm64 device",
              "images_url": "gs://fuchsia/development//images/generic-arm64.tgz",
              "name": "generic-arm64",
              "packages_url": "gs://fuchsia/development//packages/generic-arm64.tar.gz",
              "type": "device_profile"
            }"#;

            Ok(BufReader::new(META.as_bytes()))
        } else if name
            == "/fuchsia/out/default/host_x64/gen/src/developer/debug/zxdb/zxdb_sdk.meta.json"
        {
            const META: &str = r#"{
              "files": [
                "tools/x64/zxdb"
              ],
              "name": "zxdb",
              "root": "tools",
              "type": "host_tool"
            }"#;

            Ok(BufReader::new(META.as_bytes()))
        } else if name
            == "/fuchsia/out/default/host_x64/gen/tools/symbol-index/symbol_index_sdk.meta.json"
        {
            const META: &str = r#"{
              "files": [
                "tools/x64/symbol-index"
              ],
              "name": "symbol-index",
              "root": "tools",
              "type": "host_tool"
            }"#;

            Ok(BufReader::new(META.as_bytes()))
        } else if name
            == "/fuchsia/out/default/host_x64/gen/tools/symbol-index/symbol_index_sdk_legacy.meta.json"
        {
            const META: &str = r#"{
              "files": [
                "tools/x64/symbol-index"
              ],
              "name": "symbol-index",
              "root": "tools",
              "type": "host_tool"
            }"#;

            Ok(BufReader::new(META.as_bytes()))
        } else if name
            == "/fuchsia/out/default/host_arm64/gen/tools/symbol-index/symbol_index_sdk.meta.json"
        {
            const META: &str = r#"{
              "files": [
                "tools/arm64/symbol-index"
              ],
              "name": "symbol-index",
              "root": "tools",
              "type": "host_tool"
            }"#;

            Ok(BufReader::new(META.as_bytes()))
        } else {
            Err(anyhow!("No such manifest: {}", name))
        }
    }

    const SDK_MANIFEST: &str = r#"{
        "arch": {
            "host": "x86_64-linux-gnu",
            "target": [
                "arm64",
                "x64"
            ]
        },
        "id": "0.20201005.4.1",
        "parts": [
            {
              "meta": "fidl/fuchsia.data/meta.json",
              "type": "fidl_library"
            },
            {
              "meta": "tools/zxdb-meta.json",
              "type": "host_tool"
            }
        ],
        "schema_version": "1"
    }"#;

    fn get_sdk_manifest_meta(name: &str) -> Option<BufReader<&'static [u8]>> {
        if name == "fidl/fuchsia.data/meta.json" {
            const META: &str = r#"{
              "deps": [],
              "name": "fuchsia.data",
              "root": "fidl/fuchsia.data",
              "sources": [
                "fidl/fuchsia.data/data.fidl"
              ],
              "type": "fidl_library"
            }"#;

            Some(BufReader::new(META.as_bytes()))
        } else if name == "tools/zxdb-meta.json" {
            const META: &str = r#"{
              "files": [
                "tools/zxdb"
              ],
              "name": "zxdb",
              "root": "tools",
              "target_files": {},
              "type": "host_tool"
            }"#;

            Some(BufReader::new(META.as_bytes()))
        } else {
            None
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_core_manifest() {
        let manifest_path: PathBuf = PathBuf::from("/fuchsia/out/default");
        let atoms =
            Sdk::atoms_from_core_manifest(manifest_path, BufReader::new(CORE_MANIFEST.as_bytes()))
                .unwrap();

        assert!(atoms.ids.is_empty());

        let atoms = atoms.atoms;
        assert_eq!(5, atoms.len());
        assert_eq!("partner", atoms[0].category);
        assert!(atoms[0].deps.is_empty());
        assert_eq!("//sdk/devices:generic-arm64(//build/toolchain/fuchsia:x64)", atoms[0].gn_label);
        assert_eq!("sdk://device/generic-arm64", atoms[0].id);
        assert_eq!("device_profile", atoms[0].ty);
        assert_eq!(1, atoms[0].files.len());
        assert_eq!("device/generic-arm64.json", atoms[0].files[0].destination);
        assert_eq!("gen/sdk/devices/generic-arm64.meta.json", atoms[0].files[0].source);

        assert_eq!(2, atoms[1].files.len());
        assert_eq!("tools/x64/zxdb", atoms[1].files[0].destination);
        assert_eq!("host_x64/zxdb", atoms[1].files[0].source);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_core_manifest_to_sdk() {
        let manifest_path: PathBuf = PathBuf::from("/fuchsia/out/default");
        let atoms = Sdk::atoms_from_core_manifest(
            manifest_path.clone(),
            BufReader::new(CORE_MANIFEST.as_bytes()),
        )
        .unwrap();

        let sdk =
            Sdk::from_sdk_atoms(manifest_path, atoms, get_core_manifest_meta, SdkVersion::Unknown)
                .unwrap();

        assert_eq!(5, sdk.metas.len());
        assert_eq!(
            "A generic arm64 device",
            sdk.metas[0].get("description").unwrap().as_str().unwrap()
        );
        assert_eq!("host_tool", sdk.metas[1].get("type").unwrap().as_str().unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_core_manifest_host_tool() {
        let manifest_path: PathBuf = PathBuf::from("/fuchsia/out/default");
        let atoms = Sdk::atoms_from_core_manifest(
            manifest_path.clone(),
            BufReader::new(CORE_MANIFEST.as_bytes()),
        )
        .unwrap();

        let sdk =
            Sdk::from_sdk_atoms(manifest_path, atoms, get_core_manifest_meta, SdkVersion::Unknown)
                .unwrap();
        let zxdb = sdk.get_host_tool("zxdb").unwrap();

        assert_eq!(PathBuf::from("/fuchsia/out/default/host_x64/zxdb"), zxdb);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_core_manifest_host_tool_multi_arch() {
        let manifest_path: PathBuf = PathBuf::from("/fuchsia/out/default");
        let atoms = Sdk::atoms_from_core_manifest(
            manifest_path.clone(),
            BufReader::new(CORE_MANIFEST.as_bytes()),
        )
        .unwrap();

        let sdk =
            Sdk::from_sdk_atoms(manifest_path, atoms, get_core_manifest_meta, SdkVersion::Unknown)
                .unwrap();
        let symbol_index = sdk.get_host_tool("symbol-index").unwrap();

        assert_eq!(PathBuf::from("/fuchsia/out/default/host_x64/symbol-index"), symbol_index);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_sdk_manifest() {
        let mut version = SdkVersion::Unknown;
        let metas = Sdk::metas_from_sdk_manifest(
            BufReader::new(SDK_MANIFEST.as_bytes()),
            &mut version,
            get_sdk_manifest_meta,
        )
        .unwrap();

        assert_eq!(SdkVersion::Version("0.20201005.4.1".to_owned()), version);

        assert_eq!(2, metas.len());
        assert_eq!("fidl_library", metas[0].get("type").unwrap().as_str().unwrap());
        assert_eq!("host_tool", metas[1].get("type").unwrap().as_str().unwrap());
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_sdk_manifest_host_tool() {
        let metas = Sdk::metas_from_sdk_manifest(
            BufReader::new(SDK_MANIFEST.as_bytes()),
            &mut SdkVersion::Unknown,
            get_sdk_manifest_meta,
        )
        .unwrap();

        let sdk = Sdk {
            path_prefix: "/foo/bar".into(),
            metas,
            real_paths: None,
            version: SdkVersion::Unknown,
        };
        let zxdb = sdk.get_host_tool("zxdb").unwrap();

        assert_eq!(PathBuf::from("/foo/bar/tools/zxdb"), zxdb);
    }
}
