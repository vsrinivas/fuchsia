// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{asset::Asset, cache::Cache},
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_mem as mem,
    parking_lot::RwLock,
    std::{
        collections::BTreeMap,
        fs::File,
        path::{Path, PathBuf},
    },
};

/// Get `VMO` handle to the [`Asset`] at `path`.
/// TODO(seancuff): Use a typed error instead.
fn load_asset_to_vmo(path: &Path) -> Result<mem::Buffer, Error> {
    let file = File::open(path)?;
    let vmo = fdio::get_vmo_copy_from_file(&file)?;
    let size = file.metadata()?.len();
    Ok(mem::Buffer { vmo, size })
}

/// Stores the relationship between [`Asset`] paths and IDs.
/// Should be initialized by [`FontService`].
///
/// `path_to_id_map` and `id_to_path_map` form a bidirectional map, so this relation holds:
/// ```
/// assert_eq!(self.path_to_id_map.get(&path), Some(&id));
/// assert_eq!(self.id_to_path_map.get(&id), Some(&path));
/// ```
pub struct Collection {
    /// Maps [`Asset`] path to ID.
    path_to_id_map: BTreeMap<PathBuf, u32>,
    /// Inverse of `path_to_id_map`.
    id_to_path_map: BTreeMap<u32, PathBuf>,
    /// Next ID to assign, autoincremented from 0.
    next_id: u32,
    cache: RwLock<Cache>,
}

const CACHE_SIZE_BYTES: u64 = 4_000_000;

impl Collection {
    pub fn new() -> Collection {
        Collection {
            path_to_id_map: BTreeMap::new(),
            id_to_path_map: BTreeMap::new(),
            next_id: 0,
            cache: RwLock::new(Cache::new(CACHE_SIZE_BYTES)),
        }
    }

    /// Add the [`Asset`] found at `path` to the collection and return its ID.
    /// If `path` is already in the collection, return the existing ID.
    ///
    /// TODO(seancuff): Switch to updating ID of existing entries. This would allow assets to be
    /// updated without restarting the service (e.g. installing a newer version of a file). Clients
    /// would need to check the ID of their currently-held asset against the response.
    pub fn add_or_get_asset_id(&mut self, path: &Path) -> u32 {
        if let Some(id) = self.path_to_id_map.get(&path.to_path_buf()) {
            return *id;
        }
        let id = self.next_id;
        self.id_to_path_map.insert(id, path.to_path_buf());
        self.path_to_id_map.insert(path.to_path_buf(), id);
        self.next_id += 1;
        id
    }

    /// Get a `Buffer` holding the `Vmo` for the [`Asset`] corresponding to `id`, using the cache
    /// if possible.
    pub fn get_asset(&self, id: u32) -> Result<mem::Buffer, Error> {
        if let Some(path) = self.id_to_path_map.get(&id) {
            let mut cache_writer = self.cache.write();
            let buf = match cache_writer.get(id) {
                Some(cached) => cached.buffer,
                None => {
                    cache_writer
                        .push(Asset {
                            id,
                            buffer: load_asset_to_vmo(path).with_context(|_| {
                                format!("Failed to load {}", path.to_string_lossy())
                            })?,
                        })
                        .buffer
                }
            };
            return Ok(buf);
        }
        Err(format_err!("No asset found with id {}", id))
    }
}
