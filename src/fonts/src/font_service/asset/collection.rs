// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    super::{cache::Cache, Asset, AssetId, AssetLoader},
    anyhow::Error,
    fidl_fuchsia_fonts::CacheMissPolicy,
    fidl_fuchsia_io as io, fidl_fuchsia_mem as mem,
    fuchsia_inspect::{self as finspect, Property},
    fuchsia_url::pkg_url::PkgUrl,
    futures::{join, lock::Mutex},
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

    /// Returns a string representing either an absolute local path or an absolute package resource
    /// URL, e.g. "/config/data/Alpha.ttf" or
    /// "fuchsia-pkg://fuchsia.com/font-package-alpha-ttf#Alpha.ttf".
    fn path_or_url(&self) -> String {
        match &self.location {
            v2::AssetLocation::LocalFile(locator) => {
                locator.directory.join(&self.file_name).to_string_lossy().to_string()
            }
            v2::AssetLocation::Package(locator) => PkgUrl::new_resource(
                locator.url.host().to_owned(),
                locator.url.path().to_owned(),
                locator.url.package_hash().map(|s| s.to_owned()),
                self.file_name.to_owned(),
            )
            .unwrap()
            .to_string(),
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
            // TODO(fxbug.dev/45391): Factor this out into AssectCollectionInspectData
            inspect_node: AssetCollectionInspectData::make_node(parent_inspect_node),
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

    /// Updates the capacity of the in-memory cache.
    pub fn set_cache_capacity(&mut self, cache_capacity_bytes: u64) {
        self.cache_capacity_bytes = cache_capacity_bytes;
    }

    /// Consumes the builder and creates an [`AssetCollection`].
    pub fn build(self) -> AssetCollection<L> {
        let inspect_data =
            AssetCollectionInspectData::new(self.inspect_node, &self.id_to_location_map);

        AssetCollection {
            asset_loader: self.asset_loader,
            id_to_location_map: self.id_to_location_map,
            id_to_dir_map: Mutex::new(BTreeMap::new()),
            cache: Mutex::new(Cache::new(self.cache_capacity_bytes, &inspect_data.node)),
            inspect_data: Mutex::new(inspect_data),
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

    /// Inspect nodes
    inspect_data: Mutex<AssetCollectionInspectData>,
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

                let (mut cache_lock, mut inspect_data_lock) =
                    join!(self.cache.lock(), self.inspect_data.lock());

                let (asset, is_cached, popped_assets) = cache_lock.push(Asset { id, buffer });
                inspect_data_lock.update_after_cache_push(&asset, is_cached, &popped_assets);
                asset.buffer
            }
        };
        Ok(buffer)
    }

    /// Looks up the local path or URL for a given asset ID.
    pub fn get_asset_path_or_url(&self, asset_id: AssetId) -> Option<String> {
        self.id_to_location_map.get(&asset_id).map(FullAssetLocation::path_or_url)
    }

    /// Returns the number of assets in the collection
    #[allow(dead_code)]
    #[cfg(test)]
    pub fn len(&self) -> usize {
        self.id_to_location_map.len()
    }

    /// Returns the capacity of the in-memory cache in bytes.
    #[cfg(test)]
    pub async fn cache_capacity_bytes(&self) -> u64 {
        self.cache.lock().await.capacity_bytes()
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

/// Inspect data for a single asset.
#[allow(dead_code)]
struct AssetInspectData {
    node: finspect::Node,
    caching: finspect::StringProperty,
    size: Option<finspect::UintProperty>,
}

impl AssetInspectData {
    const CACHING_CACHED: &'static str = "cached";
    const CACHING_UNCACHED: &'static str = "uncached";

    fn new(parent_node: &finspect::Node, asset_id: &AssetId, location: &FullAssetLocation) -> Self {
        let node = parent_node.create_child(format!("{}", asset_id.0));
        node.record_string("location", location.path_or_url());
        let caching = node.create_string("caching", Self::CACHING_UNCACHED);
        let size = None;
        AssetInspectData { node, caching, size }
    }
}

/// Inspect data for a [`Collection`].
#[allow(dead_code)]
pub struct AssetCollectionInspectData {
    node: finspect::Node,

    assets_node: finspect::Node,
    assets: BTreeMap<AssetId, AssetInspectData>,
}

impl AssetCollectionInspectData {
    /// Creates a new inspect data container with the given _self_ node (not parent node) and asset
    /// locations.
    fn new(
        node: finspect::Node,
        id_to_location_map: &BTreeMap<AssetId, FullAssetLocation>,
    ) -> Self {
        node.record_uint("count", id_to_location_map.len() as u64);

        let assets_node = node.create_child("assets");
        let mut assets = BTreeMap::new();

        for (id, location) in id_to_location_map.iter() {
            let asset_data = AssetInspectData::new(&assets_node, id, location);
            assets.insert(id.clone(), asset_data);
        }

        AssetCollectionInspectData { node, assets_node, assets }
    }

    /// Creates an Inspect node to be passed into `new`.
    fn make_node(parent_node: &finspect::Node) -> finspect::Node {
        parent_node.create_child("asset_collection")
    }

    /// Updates the state of the inspect data based on the result of `Cache::push`.
    fn update_after_cache_push(&mut self, asset: &Asset, is_cached: bool, popped_assets: &[Asset]) {
        if is_cached {
            self.set_file_cached(asset.id, asset.buffer.size as usize);
        }
        for popped_asset in popped_assets {
            self.set_not_cached(popped_asset.id);
        }
    }

    /// Marks the given asset as cached and keeps track of its size in bytes.
    fn set_file_cached(&mut self, asset_id: AssetId, size: usize) {
        self.assets.entry(asset_id).and_modify(|asset_data| {
            asset_data.caching.set(AssetInspectData::CACHING_CACHED);
            let node = &mut asset_data.node;
            asset_data.size.get_or_insert_with(|| node.create_uint("size_bytes", size as u64));
        });
    }

    /// Marks the given asset as no longer cached. (The size data is kept.)
    fn set_not_cached(&mut self, asset_id: AssetId) {
        self.assets.entry(asset_id).and_modify(|asset_data| {
            asset_data.caching.set(AssetInspectData::CACHING_UNCACHED);
        });
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, async_trait::async_trait, finspect::assert_inspect_tree, fuchsia_async as fasync,
        fuchsia_zircon as zx, std::path::Path,
    };

    fn mock_vmo(vmo_size: u64, buffer_size: u64) -> mem::Buffer {
        mem::Buffer { vmo: zx::Vmo::create(vmo_size).unwrap(), size: buffer_size }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_inspect_data() -> Result<(), Error> {
        struct FakeAssetLoader {}

        #[async_trait]
        #[allow(dead_code, unused_variables)]
        impl AssetLoader for FakeAssetLoader {
            async fn fetch_package_directory(
                &self,
                package_locator: &v2::PackageLocator,
            ) -> Result<io::DirectoryProxy, AssetCollectionError> {
                unimplemented!()
            }

            fn load_vmo_from_path(&self, path: &Path) -> Result<mem::Buffer, AssetCollectionError> {
                let size = match path.file_name().unwrap().to_str().unwrap() {
                    "Alpha-Regular.ttf" => 2345,
                    "Beta-Regular.ttf" => 4000,
                    _ => unreachable!(),
                };
                Ok(mock_vmo(size * 2, size))
            }

            async fn load_vmo_from_directory_proxy(
                &self,
                directory_proxy: io::DirectoryProxy,
                package_locator: &v2::PackageLocator,
                file_name: &str,
            ) -> Result<mem::Buffer, AssetCollectionError> {
                unimplemented!()
            }
        }

        let inspector = finspect::Inspector::new();

        let assets = vec![
            v2::Asset {
                file_name: "Alpha-Regular.ttf".to_string(),
                location: v2::AssetLocation::LocalFile(v2::LocalFileLocator {
                    directory: "/config/data/assets".into(),
                }),
                typefaces: vec![],
            },
            v2::Asset {
                file_name: "Beta-Regular.ttf".to_string(),
                location: v2::AssetLocation::LocalFile(v2::LocalFileLocator {
                    directory: "/config/data/assets".into(),
                }),
                typefaces: vec![],
            },
            v2::Asset {
                file_name: "Alpha-Condensed.ttf".to_string(),
                location: v2::AssetLocation::Package(v2::PackageLocator {
                    url: PkgUrl::parse(
                        "fuchsia-pkg://fuchsia.com/font-package-alpha-condensed-ttf",
                    )?,
                }),
                typefaces: vec![],
            },
        ];
        let cache_capacity_bytes = 5000;

        let asset_loader = FakeAssetLoader {};
        let mut builder =
            AssetCollectionBuilder::new(asset_loader, cache_capacity_bytes, inspector.root());
        for asset in &assets {
            builder.add_or_get_asset_id(asset);
        }
        let collection = builder.build();

        // Note partial `contains` match. Cache is tested separately.
        assert_inspect_tree!(inspector, root: {
            asset_collection: {
                assets: {
                    "0": {
                        caching: "uncached",
                        location: "/config/data/assets/Alpha-Regular.ttf"
                    },
                    "1": {
                        caching: "uncached",
                        location: "/config/data/assets/Beta-Regular.ttf",
                    },
                    "2": {
                        caching: "uncached",
                        location: "fuchsia-pkg://fuchsia.com/font-package-alpha-condensed-ttf#Alpha-Condensed.ttf"
                    }
                },
                asset_cache: contains {},
                count: 3u64
            }
        });

        collection.get_asset(AssetId(0), CacheMissPolicy::BlockUntilDownloaded).await?;

        assert_inspect_tree!(inspector, root: {
            asset_collection: contains {
                assets: {
                    "0": {
                        caching: "cached",
                        location: "/config/data/assets/Alpha-Regular.ttf",
                        size_bytes: 2345u64,
                    },
                    "1": {
                        caching: "uncached",
                        location: "/config/data/assets/Beta-Regular.ttf",
                    },
                    "2": {
                        caching: "uncached",
                        location: "fuchsia-pkg://fuchsia.com/font-package-alpha-condensed-ttf#Alpha-Condensed.ttf"
                    }
                }
            }
        });

        collection.get_asset(AssetId(1), CacheMissPolicy::BlockUntilDownloaded).await?;
        // Asset 1 gets cached, asset 1 is evicted but size info is kept.
        assert_inspect_tree!(inspector, root: {
            asset_collection: contains {
                assets: {
                    "0": {
                        caching: "uncached",
                        location: "/config/data/assets/Alpha-Regular.ttf",
                        size_bytes: 2345u64,
                    },
                    "1": {
                        caching: "cached",
                        location: "/config/data/assets/Beta-Regular.ttf",
                        size_bytes: 4000u64,
                    },
                    "2": {
                        caching: "uncached",
                        location: "fuchsia-pkg://fuchsia.com/font-package-alpha-condensed-ttf#Alpha-Condensed.ttf"
                    }
                }
            }
        });

        Ok(())
    }
}
