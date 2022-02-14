// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, bail, Context, Result},
    errors::ffx_error,
    glob::glob as _glob,
    serde::{Deserialize, Serialize},
    std::{
        collections::HashSet,
        fs::File,
        path::{Path, PathBuf},
    },
};

pub fn global_symbol_index_path() -> Result<String> {
    Ok(std::env::var("HOME").context("getting $HOME")? + "/.fuchsia/debug/symbol-index.json")
}

// Ensures that symbols in sdk.root are registered in the global symbol index.
pub async fn ensure_symbol_index_registered() -> Result<()> {
    let sdk = ffx_config::get_sdk().await?;

    let mut symbol_index_path_str: Option<String> = None;
    let mut default_symbol_server: Option<&'static str> = None;
    let mut build_id_dir_str: Option<String> = None;
    if sdk.get_version() == &ffx_config::sdk::SdkVersion::InTree {
        let symbol_index_path = sdk.get_path_prefix().join(".symbol-index.json");
        if !symbol_index_path.exists() {
            bail!("Required {:?} doesn't exist", symbol_index_path);
        }
        symbol_index_path_str = Some(pathbuf_to_string(symbol_index_path)?);
    } else {
        let symbol_index_path = sdk.get_path_prefix().join("data/config/symbol-index/config.json");
        if symbol_index_path.exists() {
            // It's allowed that SDK do not provide a symbol-index config.
            symbol_index_path_str = Some(pathbuf_to_string(symbol_index_path)?);
        }
        let build_id_dir = sdk.get_path_prefix().join(".build-id");
        if build_id_dir.exists() {
            build_id_dir_str = Some(pathbuf_to_string(build_id_dir)?)
        }
        // The default symbol server is only needed for SDK users.
        default_symbol_server = Some("gs://fuchsia-artifacts/debug");
    }

    let global_symbol_index = global_symbol_index_path()?;
    let mut index = SymbolIndex::load(&global_symbol_index).unwrap_or(SymbolIndex::new());
    let mut needs_save = false;
    if let Some(path) = symbol_index_path_str {
        if !index.includes.contains(&path) {
            index.includes.push(path);
            needs_save = true;
        }
    }
    if let Some(server) = default_symbol_server {
        if !index.gcs_flat.iter().any(|gcs_flat| gcs_flat.url == server) {
            index.gcs_flat.push(GcsFlat { url: server.to_owned(), require_authentication: false });
            needs_save = true;
        }
    }
    if let Some(path) = build_id_dir_str {
        if !index.build_id_dirs.iter().any(|dir| dir.path == path) {
            index.build_id_dirs.push(BuildIdDir { path, build_dir: None });
            needs_save = true;
        }
    }
    if needs_save {
        index.save(&global_symbol_index)?;
    }
    return Ok(());
}

#[derive(Serialize, Deserialize, Debug)]
pub struct BuildIdDir {
    pub path: String,
    pub build_dir: Option<String>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct IdsTxt {
    pub path: String,
    pub build_dir: Option<String>,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct GcsFlat {
    pub url: String,
    #[serde(default)]
    pub require_authentication: bool,
}

#[derive(Serialize, Deserialize, Debug)]
pub struct SymbolIndex {
    #[serde(default)]
    pub includes: Vec<String>,
    #[serde(default)]
    pub build_id_dirs: Vec<BuildIdDir>,
    #[serde(default)]
    pub ids_txts: Vec<IdsTxt>,
    #[serde(default)]
    pub gcs_flat: Vec<GcsFlat>,
}

impl SymbolIndex {
    pub fn new() -> Self {
        Self {
            includes: Vec::new(),
            build_id_dirs: Vec::new(),
            ids_txts: Vec::new(),
            gcs_flat: Vec::new(),
        }
    }

    /// Load a file at given path.
    pub fn load(path: &str) -> Result<SymbolIndex> {
        let mut index: SymbolIndex = serde_json::from_reader(
            File::open(path).map_err(|_| ffx_error!("Cannot open {}", path))?,
        )
        .map_err(|_| ffx_error!("Cannot parse {}", path))?;

        // Resolve all paths to absolute.
        let mut base = PathBuf::from(path);
        base.pop(); // Only keeps the directory part.
        index.includes =
            index.includes.into_iter().flat_map(|p| glob(resolve_path(&base, &p))).collect();
        index.build_id_dirs = index
            .build_id_dirs
            .into_iter()
            .flat_map(|build_id_dir| {
                let build_dir = build_id_dir.build_dir.map(|p| resolve_path(&base, &p));
                glob(resolve_path(&base, &build_id_dir.path))
                    .into_iter()
                    .map(move |p| BuildIdDir { path: p, build_dir: build_dir.clone() })
            })
            .collect();
        index.ids_txts = index
            .ids_txts
            .into_iter()
            .flat_map(|ids_txt| {
                let build_dir = ids_txt.build_dir.map(|p| resolve_path(&base, &p));
                glob(resolve_path(&base, &ids_txt.path))
                    .into_iter()
                    .map(move |p| IdsTxt { path: p, build_dir: build_dir.clone() })
            })
            .collect();
        Ok(index)
    }

    /// Load and aggregate all the includes.
    pub fn load_aggregate(path: &str) -> Result<SymbolIndex> {
        let mut index = SymbolIndex::new();
        let mut visited: HashSet<String> = HashSet::new();
        index.includes.push(path.to_owned());

        while let Some(include) = index.includes.pop() {
            if visited.contains(&include) {
                continue;
            }
            match Self::load(&include) {
                Ok(mut sub) => {
                    index.includes.append(&mut sub.includes);
                    index.build_id_dirs.append(&mut sub.build_id_dirs);
                    index.ids_txts.append(&mut sub.ids_txts);
                    index.gcs_flat.append(&mut sub.gcs_flat);
                }
                Err(err) => {
                    // Only report error for the main entry.
                    if visited.is_empty() {
                        return Err(err);
                    }
                }
            }
            visited.insert(include);
        }
        Ok(index)
    }

    pub fn save(&self, path: &str) -> Result<()> {
        serde_json::to_writer_pretty(
            File::create(path).map_err(|_| ffx_error!("Cannot save to {}", path))?,
            self,
        )
        .map_err(|err| err.into())
    }
}

/// Resolve a relative path against a base directory, with possible ".."s at the beginning.
/// If the relative is actually absolute, return it directly. The base directory could be either
/// an absolute path or a relative path.
///
/// Unlike std::fs::canonicalize, this method does not require paths to exist and it does not
/// resolve symbolic links.
pub fn resolve_path(base: &Path, relative: &str) -> String {
    let mut path = base.to_owned();
    for comp in Path::new(relative).components() {
        match comp {
            std::path::Component::RootDir => {
                path.push(relative);
                break;
            }
            std::path::Component::ParentDir => {
                path.pop();
            }
            std::path::Component::Normal(name) => {
                path.push(name);
            }
            _ => {}
        }
    }
    path.into_os_string().into_string().unwrap()
}

fn glob(path: String) -> Vec<String> {
    // Only glob if "*" is in the string.
    if path.contains('*') {
        _glob(&path)
            .map(|paths| {
                paths
                    .filter_map(|res| {
                        res.ok().map(|p| p.into_os_string().into_string().ok()).flatten()
                    })
                    .collect()
            })
            .unwrap_or(vec![path])
    } else {
        vec![path]
    }
}

fn pathbuf_to_string(pathbuf: PathBuf) -> Result<String> {
    pathbuf
        .into_os_string()
        .into_string()
        .map_err(|s| anyhow!("Cannot convert OsString {:?} into String", s))
}

#[cfg(test)]
mod tests {
    use super::*;

    const TEST_DATA_DIR: &str = "../../src/developer/ffx/lib/symbol-index/test_data";

    #[test]
    fn test_load() {
        let index = SymbolIndex::load(&format!("{}/main.json", TEST_DATA_DIR)).unwrap();
        assert_eq!(index.includes.len(), 2);
        assert_eq!(index.build_id_dirs.len(), 1);
        assert_eq!(index.build_id_dirs[0].path, "/home/someone/.fuchsia/debug/symbol-cache");
        assert_eq!(index.ids_txts.len(), 1);
        assert_eq!(index.ids_txts[0].path, format!("{}/ids.txt", TEST_DATA_DIR));
        assert_eq!(
            index.ids_txts[0].build_dir,
            Some(str::replace(TEST_DATA_DIR, "test_data", "build_dir"))
        );
        assert_eq!(index.gcs_flat.len(), 1);
        assert_eq!(index.gcs_flat[0].require_authentication, false);
    }

    #[test]
    fn test_aggregate() {
        let index = SymbolIndex::load_aggregate(&format!("{}/main.json", TEST_DATA_DIR)).unwrap();
        assert_eq!(index.includes.len(), 0);
        assert_eq!(index.gcs_flat.len(), 2);
        assert_eq!(index.gcs_flat[1].require_authentication, true);
    }
}
