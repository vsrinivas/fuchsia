// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{cache::Cache, Asset, AssetId, AssetLoader},
    anyhow::Error,
    fidl_fuchsia_fonts::CacheMissPolicy,
    fidl_fuchsia_io as io, fidl_fuchsia_mem as mem, fuchsia_inspect as finspect,
    futures::lock::Mutex,
    manifest::v2,
    std::{collections::BTreeMap, hash::Hash, path::PathBuf},
    thiserror::Error,
};

/// A complete asset location, including the file name. (`AssetLocation` only includes the
/// directory or package.)
#[derive(Clone, Debug, Eq, PartialEq, Hash, Ord, PartialOrd)]
struct FullAssetLocation {
    /// Directory or package location the file.
    location: v2::AssetLocation,
    /// Name of the font asset file.
    file_name: String,
}

impl FullAssetLocation {
    /// If the asset represents a local file, returns the package-relative path to the file.
    /// Otherwise, returns `None`.
    pub fn local_path(&self) -> Option<PathBuf> {
        match &self.location {
            v2::AssetLocation::LocalFile(locator) => {
                Some(locator.directory.join(self.file_name.clone()))
            }
            _ => None,
        }
    }
}

/// Builder for [`AssetCollection`]. Allows populating all the fields that are then immutable for
/// the lifetime of the `AssetCollection`.
///
/// Create an instance with [`new()`](AssetCollectionBuilder::new), then populate using
/// [`add_or_get_asset_id()`](AssetCollectionBuilder::add_or_get_asset_id), and finally construct
/// an `AssetCollection` using [`build()`](AssetCollectionBuilder::build).
///
/// The builder is consumed when `build()` is called.
#[derive(Debug)]
pub struct AssetCollectionBuilder<L>
where
    L: AssetLoader,
{
    asset_loader: L,
    cache_capacity_bytes: u64,
    id_to_location_map: BTreeMap<AssetId, FullAssetLocation>,
    /// Used for quickly looking up assets that have already been added.
    location_to_id_map: BTreeMap<FullAssetLocation, AssetId>,
    /// Used for looking up assets for the fallback chain.
    file_name_to_id_map: BTreeMap<String, AssetId>,
    next_id: u32,
    inspect_node: finspect::Node,
}

impl<L> AssetCollectionBuilder<L>
where
    L: AssetLoader,
{
    /// Creates a new, empty `AssetCollectionBuilder` with the given asset loader, cache capacity,
    /// and parent Inspect node.
    ///
    /// A child node for the asset collection is created immediately in the builder, to avoid having
    /// to manage the lifetime of the borrowed parent node. (If the builder is dropped without
    /// calling `build()`, the node is also dropped.)
    pub fn new(
        asset_loader: L,
        cache_capacity_bytes: u64,
        parent_inspect_node: &finspect::Node,
    ) -> AssetCollectionBuilder<L> {
        AssetCollectionBuilder {
            asset_loader,
            cache_capacity_bytes,
            id_to_location_map: BTreeMap::new(),
            location_to_id_map: BTreeMap::new(),
            file_name_to_id_map: BTreeMap::new(),
            next_id: 0,
            // TODO(fxb/45391): Factor this out into AssectCollectionInspectData
            inspect_node: parent_inspect_node.create_child("asset_collection"),
        }
    }

    /// Adds a [manifest `Asset`](manifest::v2::Asset) to the builder and returns a new or existing
    /// asset ID.
    pub fn add_or_get_asset_id(&mut self, manifest_asset: &v2::Asset) -> AssetId {
        let full_location = FullAssetLocation {
            file_name: manifest_asset.file_name.clone(),
            location: manifest_asset.location.clone(),
        };
        if let Some(id) = self.location_to_id_map.get(&full_location) {
            return *id;
        }
        let id = AssetId(self.next_id);
        self.id_to_location_map.insert(id, full_location.clone());
        self.location_to_id_map.insert(full_location.clone(), id);
        self.file_name_to_id_map.insert(manifest_asset.file_name.clone(), id);
        self.next_id += 1;
        id
    }

    /// Looks up the existing `AssetId` for a given asset file name.
    pub fn get_asset_id_by_name(&self, file_name: &String) -> Option<AssetId> {
        self.file_name_to_id_map.get(file_name).map(|asset_id| *asset_id)
    }

    /// Consumes the builder and creates an [`AssetCollection`].
    pub fn build(self) -> AssetCollection<L> {
        AssetCollection {
            asset_loader: self.asset_loader,
            id_to_location_map: self.id_to_location_map,
            id_to_dir_map: Mutex::new(BTreeMap::new()),
            cache: Mutex::new(Cache::new(self.cache_capacity_bytes, &self.inspect_node)),
        }
    }
}

/// A lazy collection of `mem::Buffer`s, each representing a single font asset (file).
///
/// Assets can be retrieved by [`AssetId`] (which are assigned at service startup). The collection
/// knows the location (local file or Fuchsia package) of every `AssetId`.
#[derive(Debug)]
pub struct AssetCollection<L: AssetLoader> {
    asset_loader: L,

    /// Maps asset ID to its location. Immutable over the lifetime of the collection.
    id_to_location_map: BTreeMap<AssetId, FullAssetLocation>,

    /// Maps asset IDs to previously-resolved directory handles for packages.
    ///
    /// `DirectoryProxy` contains an `Arc`, so it is safe to clone. We can use a directory for an
    /// extended period without keeping the entire map locked.
    //
    // TODO(8904): If resource use becomes a concern, consider replacing with a LRU cache.
    id_to_dir_map: Mutex<BTreeMap<AssetId, io::DirectoryProxy>>,

    /// Cache of memory buffers
    cache: Mutex<Cache>,
}

impl<L: AssetLoader> AssetCollection<L> {
    /// Gets a `Buffer` holding the `Vmo` for the [`Asset`] corresponding to `id`, using the cache
    /// if possible. If the `Buffer` is not cached, this may involve reading a file on disk or even
    /// resolving an ephemeral package.
    pub async fn get_asset(
        &self,
        id: AssetId,
        cache_miss_policy: CacheMissPolicy,
    ) -> Result<mem::Buffer, AssetCollectionError> {
        let location =
            self.id_to_location_map.get(&id).ok_or_else(|| AssetCollectionError::UnknownId(id))?;

        // Note that we don't keep the cache locked while fetching.
        let mut cache_lock = self.cache.lock().await;
        let buffer = match cache_lock.get(id) {
            Some(cached_asset) => {
                drop(cache_lock);
                cached_asset.buffer
            }
            None => {
                drop(cache_lock);
                let buffer = match &location.location {
                    v2::AssetLocation::LocalFile(_) => {
                        let file_path = location.local_path().unwrap();
                        self.asset_loader.load_vmo_from_path(&file_path)
                    }
                    v2::AssetLocation::Package(package_locator) => {
                        self.get_package_asset(
                            id,
                            package_locator,
                            &location.file_name,
                            cache_miss_policy,
                        )
                        .await
                    }
                }?;
                let mut cache_lock = self.cache.lock().await;
                let (asset, _, _) = cache_lock.push(Asset { id, buffer });
                asset.buffer
            }
        };
        Ok(buffer)
    }

    /// Returns the number of assets in the collection
    #[allow(dead_code)]
    #[cfg(test)]
    pub fn len(&self) -> usize {
        self.id_to_location_map.len()
    }

    /// Gets a `Buffer` for the [`Asset`] corresponding to `id` from a Fuchsia package, using the
    /// directory proxy cache if possible.
    async fn get_package_asset(
        &self,
        id: AssetId,
        package_locator: &v2::PackageLocator,
        file_name: &str,
        policy: CacheMissPolicy,
    ) -> Result<mem::Buffer, AssetCollectionError> {
        // Note that we don't keep the cache locked while fetching.
        let directory_cache_lock = self.id_to_dir_map.lock().await;
        let directory_proxy = match directory_cache_lock.get(&id) {
            Some(dir_proxy) => {
                let dir_proxy = Clone::clone(dir_proxy);
                drop(directory_cache_lock);
                Ok(dir_proxy)
            }
            None => {
                match policy {
                    CacheMissPolicy::BlockUntilDownloaded => {
                        drop(directory_cache_lock);
                        self.fetch_and_cache_package_directory(id, package_locator).await
                    }
                    // TODO(8904): Implement async fetching and notification
                    _ => Err(AssetCollectionError::UncachedEphemeral {
                        file_name: file_name.to_string(),
                        package_locator: package_locator.clone(),
                    }),
                }
            }
        }?;

        self.asset_loader
            .load_vmo_from_directory_proxy(directory_proxy, package_locator, file_name)
            .await
    }

    /// Resolves the requested package and adds its `Directory` to our cache.
    async fn fetch_and_cache_package_directory(
        &self,
        asset_id: AssetId,
        package_locator: &v2::PackageLocator,
    ) -> Result<io::DirectoryProxy, AssetCollectionError> {
        let dir_proxy = self.asset_loader.fetch_package_directory(package_locator).await?;

        // Cache directory handle
        let mut directory_cache = self.id_to_dir_map.lock().await;
        directory_cache.insert(asset_id, Clone::clone(&dir_proxy));
        // TODO(8904): For "universe" fonts, send event to clients.

        Ok(dir_proxy)
    }
}

/// Possible errors when trying to retrieve an asset `Buffer` from the collection.
#[derive(Debug, Error)]
pub enum AssetCollectionError {
    /// Unknown `AssetId`
    #[error("No asset found with id {:?}", _0)]
    UnknownId(AssetId),

    /// A file within the font service's namespace could not be accessed
    #[error("Local file not accessible: {:?}", _0)]
    LocalFileNotAccessible(PathBuf, #[source] Error),

    /// Failed to connect to the Font Resolver service
    #[error("Service connection error: {:?}", _0)]
    ServiceConnectionError(#[source] Error),

    /// The Font Resolver could not resolve the requested package
    #[error("Package resolver error when resolving {:?}: {:?}", _0, _1)]
    PackageResolverError(v2::PackageLocator, #[source] Error),

    /// The requested package was resolved, but its font file could not be read
    #[error("Error when reading file {:?} from {:?}: {:?}", file_name, package_locator, cause)]
    PackagedFileError {
        /// Name of the file
        file_name: String,
        /// Package that we're trying to read
        package_locator: v2::PackageLocator,
        #[source]
        cause: Error,
    },

    /// The client requested an empty response if the asset wasn't cached, and the asset is indeed
    /// not cached
    #[allow(dead_code)]
    #[error("Requested asset needs to be loaded ephemerally but is not yet cached")]
    UncachedEphemeral {
        /// Name of the file
        file_name: String,
        /// Package that we want to read
        package_locator: v2::PackageLocator,
    },
}
