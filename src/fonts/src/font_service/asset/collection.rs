// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{asset::Asset, cache::Cache},
    failure::{format_err, Error, ResultExt},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_io as io, fidl_fuchsia_mem as mem,
    fidl_fuchsia_pkg::FontResolverMarker,
    fuchsia_component::client::connect_to_service,
    fuchsia_url::pkg_url::PkgUrl,
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    io_util,
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
    /// Maps asset paths to package URLs.
    path_to_url_map: BTreeMap<PathBuf, PkgUrl>,
    /// Maps asset paths to previously-resolved directory handles.
    path_to_dir_map: Mutex<BTreeMap<PathBuf, io::DirectoryProxy>>,
    /// Next ID to assign, autoincremented from 0.
    next_id: u32,
    cache: Mutex<Cache>,
}

const CACHE_SIZE_BYTES: u64 = 4_000_000;

impl Collection {
    pub fn new() -> Collection {
        Collection {
            path_to_id_map: BTreeMap::new(),
            id_to_path_map: BTreeMap::new(),
            path_to_url_map: BTreeMap::new(),
            path_to_dir_map: Mutex::new(BTreeMap::new()),
            next_id: 0,
            cache: Mutex::new(Cache::new(CACHE_SIZE_BYTES)),
        }
    }

    /// Add the [`Asset`] found at `path` to the collection, store its package URL if provided,
    /// and return the asset's ID.
    /// If `path` is already in the collection, return the existing ID.
    pub fn add_or_get_asset_id(&mut self, path: &Path, package_url: Option<&PkgUrl>) -> u32 {
        if let Some(id) = self.path_to_id_map.get(&path.to_path_buf()) {
            return *id;
        }
        let id = self.next_id;
        self.id_to_path_map.insert(id, path.to_path_buf());
        self.path_to_id_map.insert(path.to_path_buf(), id);
        if let Some(url) = package_url {
            self.path_to_url_map.insert(path.to_path_buf(), url.clone());
        }
        self.next_id += 1;
        id
    }

    /// Get a `Buffer` holding the `Vmo` for the [`Asset`] corresponding to `id`, using the cache
    /// if possible.
    pub async fn get_asset(&self, id: u32) -> Result<mem::Buffer, Error> {
        if let Some(path) = self.id_to_path_map.get(&id) {
            let mut cache_lock = self.cache.lock().await;
            let buf = match cache_lock.get(id) {
                Some(cached) => cached.buffer,
                None => {
                    let buffer = if path.exists() {
                        load_asset_to_vmo(path).with_context(|_| {
                            format!("Failed to load {}.", path.to_string_lossy())
                        })?
                    } else {
                        self.get_ephemeral_asset(path).await?
                    };

                    cache_lock.push(Asset { id, buffer }).buffer
                }
            };
            return Ok(buf);
        }
        Err(format_err!("No asset found with id {}", id))
    }

    async fn get_ephemeral_asset(&self, path_buf: &PathBuf) -> Result<mem::Buffer, Error> {
        let filename = path_buf.as_path().file_name().ok_or(format_err!(
            "Path '{}' does not contain a valid filename.",
            path_buf.to_string_lossy()
        ))?;

        // Get cached directory if it is cached
        let mut cache_lock = self.path_to_dir_map.lock().await;

        let directory_proxy = match cache_lock.get(path_buf) {
            Some(dir_proxy) => dir_proxy,
            None => {
                let url = self.path_to_url_map.get(path_buf).ok_or(format_err!(
                    "No asset found with path {}",
                    path_buf.to_string_lossy()
                ))?;

                // Get directory handle from FontResolver
                let font_resolver = connect_to_service::<FontResolverMarker>()?;
                let (dir_proxy, dir_request) = create_proxy::<io::DirectoryMarker>()?;

                let status = font_resolver.resolve(&url.to_string(), dir_request).await?;
                zx::Status::ok(status)?;

                // Cache directory handle
                cache_lock.insert(path_buf.to_path_buf(), dir_proxy);
                cache_lock.get(path_buf).unwrap() // Safe because just inserted
            }
        };

        let file_proxy =
            io_util::open_file(directory_proxy, Path::new(&filename), io::OPEN_RIGHT_READABLE)?;

        drop(cache_lock);

        let (status, buffer) = file_proxy.get_buffer(io::VMO_FLAG_READ).await?;
        zx::Status::ok(status)?;

        let buffer = *buffer
            .ok_or(format_err!("Failed to get buffer for {}.", filename.to_string_lossy()))?;
        Ok(buffer)
    }
}
