// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    errors::ffx_error,
    glob::glob as _glob,
    serde::{Deserialize, Serialize},
    std::{
        collections::HashSet,
        fs::File,
        path::{Path, PathBuf},
    },
};

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
/// Unlink std::fs::canonicalize, this method does not require paths to exist and it does not
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

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_load() {
        let index = SymbolIndex::load("test_data/ffx_debug_symbol_index/main.json").unwrap();
        assert_eq!(index.includes.len(), 2);
        assert_eq!(index.build_id_dirs.len(), 1);
        assert_eq!(index.build_id_dirs[0].path, "/home/someone/.fuchsia/debug/symbol-cache");
        assert_eq!(index.ids_txts.len(), 1);
        assert_eq!(index.ids_txts[0].path, "test_data/ffx_debug_symbol_index/ids.txt");
        assert_eq!(index.ids_txts[0].build_dir, Some("test_data/build_dir".to_owned()));
        assert_eq!(index.gcs_flat.len(), 1);
        assert_eq!(index.gcs_flat[0].require_authentication, false);
    }

    #[test]
    fn test_aggregate() {
        let index =
            SymbolIndex::load_aggregate("test_data/ffx_debug_symbol_index/main.json").unwrap();
        assert_eq!(index.includes.len(), 0);
        assert_eq!(index.gcs_flat.len(), 2);
        assert_eq!(index.gcs_flat[1].require_authentication, true);
    }
}
