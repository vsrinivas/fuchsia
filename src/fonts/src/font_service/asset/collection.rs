// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use manifest::v2::PackageLocator;
use {
    super::{
        asset::{Asset, AssetId},
        cache::Cache,
    },
    clonable_error::ClonableError,
    failure::{format_err, Error, Fail},
    fidl::endpoints::create_proxy,
    fidl_fuchsia_fonts::CacheMissPolicy,
    fidl_fuchsia_io as io, fidl_fuchsia_mem as mem,
    fidl_fuchsia_pkg::{FontResolverMarker, UpdatePolicy},
    fuchsia_component::client::connect_to_service,
    fuchsia_zircon as zx,
    futures::lock::Mutex,
    io_util,
    manifest::v2,
    std::{
        collections::BTreeMap,
        fs::File,
        path::{Path, PathBuf},
    },
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
pub struct AssetCollectionBuilder {
    id_to_location_map: BTreeMap<AssetId, FullAssetLocation>,
    /// Used for quickly looking up assets that have already been added.
    location_to_id_map: BTreeMap<FullAssetLocation, AssetId>,
    next_id: u32,
}

impl AssetCollectionBuilder {
    const CACHE_SIZE_BYTES: u64 = 4_000_000;

    /// Creates a new, empty `AssetCollectionBuilder`.
    pub fn new() -> AssetCollectionBuilder {
        AssetCollectionBuilder {
            id_to_location_map: BTreeMap::new(),
            location_to_id_map: BTreeMap::new(),
            next_id: 0,
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
        self.next_id += 1;
        id
    }

    /// Build an [`AssetCollection`].
    pub fn build(self) -> AssetCollection {
        AssetCollection {
            id_to_location_map: self.id_to_location_map,
            id_to_dir_map: Mutex::new(BTreeMap::new()),
            cache: Mutex::new(Cache::new(Self::CACHE_SIZE_BYTES)),
        }
    }
}

/// A lazy collection of `mem::Buffer`s, each representing a single font asset (file).
///
/// Assets can be retrieved by [`AssetId`] (which are assigned at service startup). The collection
/// knows the location (local file or Fuchsia package) of every `AssetId`.
pub struct AssetCollection {
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

impl AssetCollection {
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
                        Self::load_asset_to_vmo(&file_path)
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
                cache_lock.push(Asset { id, buffer }).buffer
            }
        };
        Ok(buffer)
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

        Self::load_buffer_from_directory_proxy(directory_proxy, package_locator, file_name).await
    }

    /// Resolves the requested package and adds its `Directory` to our cache.
    async fn fetch_and_cache_package_directory(
        &self,
        asset_id: AssetId,
        package_locator: &v2::PackageLocator,
    ) -> Result<io::DirectoryProxy, AssetCollectionError> {
        // Get directory handle from FontResolver
        let font_resolver = connect_to_service::<FontResolverMarker>()
            .map_err(|e| AssetCollectionError::ServiceConnectionError(Error::from(e).into()))?;
        let mut update_policy = UpdatePolicy { fetch_if_absent: true, allow_old_versions: false };
        let (dir_proxy, dir_request) = create_proxy::<io::DirectoryMarker>()
            .map_err(|e| AssetCollectionError::ServiceConnectionError(Error::from(e).into()))?;

        let status = font_resolver
            .resolve(&package_locator.url.to_string(), &mut update_policy, dir_request)
            .await
            .map_err(|e| {
                AssetCollectionError::PackageResolverError(
                    package_locator.clone(),
                    Error::from(e).into(),
                )
            })?;
        zx::Status::ok(status).map_err(|e| {
            AssetCollectionError::PackageResolverError(
                package_locator.clone(),
                Error::from(e).into(),
            )
        })?;

        // Cache directory handle
        let mut directory_cache = self.id_to_dir_map.lock().await;
        directory_cache.insert(asset_id, Clone::clone(&dir_proxy));
        // TODO(8904): For "universe" fonts, send event to clients.

        Ok(dir_proxy)
    }

    async fn load_buffer_from_directory_proxy(
        directory_proxy: io::DirectoryProxy,
        package_locator: &v2::PackageLocator,
        file_name: &str,
    ) -> Result<mem::Buffer, AssetCollectionError> {
        let packaged_file_error = |cause: ClonableError| AssetCollectionError::PackagedFileError {
            file_name: file_name.to_string(),
            package_locator: package_locator.clone(),
            cause,
        };

        let file_proxy =
            io_util::open_file(&directory_proxy, Path::new(&file_name), io::OPEN_RIGHT_READABLE)
                .map_err(|e| packaged_file_error(e.into()))?;

        let (status, buffer) = file_proxy
            .get_buffer(io::VMO_FLAG_READ)
            .await
            .map_err(|e| packaged_file_error(Error::from(e).into()))?;

        zx::Status::ok(status).map_err(|e| packaged_file_error(Error::from(e).into()))?;

        buffer.map(|b| *b).ok_or_else(|| {
            packaged_file_error(
                format_err!(
                    "Inexplicably failed to access buffer after opening the file successfully"
                )
                .into(),
            )
        })
    }

    /// Gets `VMO` handle to the [`Asset`] at `path`.
    pub(crate) fn load_asset_to_vmo(path: &Path) -> Result<mem::Buffer, AssetCollectionError> {
        let file = File::open(path).map_err(|e| {
            AssetCollectionError::LocalFileNotAccessible(path.to_owned(), Error::from(e).into())
        })?;
        let vmo = fdio::get_vmo_copy_from_file(&file).map_err(|e| {
            AssetCollectionError::LocalFileNotAccessible(path.to_owned(), Error::from(e).into())
        })?;
        let size = file
            .metadata()
            .map_err(|e| {
                AssetCollectionError::LocalFileNotAccessible(path.to_owned(), Error::from(e).into())
            })?
            .len();
        Ok(mem::Buffer { vmo, size })
    }
}

/// Possible errors when trying to retrieve an asset `Buffer` from the collection.
#[derive(Debug, Fail)]
pub enum AssetCollectionError {
    /// Unknown `AssetId`
    #[fail(display = "No asset found with id {:?}", _0)]
    UnknownId(AssetId),

    /// A file within the font service's namespace could not be accessed
    #[fail(display = "Local file not accessible: {:?}", _0)]
    LocalFileNotAccessible(PathBuf, #[fail(cause)] ClonableError),

    /// Failed to connect to the Font Resolver service
    #[fail(display = "Service connection error: {:?}", _0)]
    ServiceConnectionError(#[fail(cause)] ClonableError),

    /// The Font Resolver could not resolve the requested package
    #[fail(display = "Package resolver error when resolving {:?}: {:?}", _0, _1)]
    PackageResolverError(PackageLocator, #[fail(cause)] ClonableError),

    /// The requested package was resolved, but its font file could not be read
    #[fail(
        display = "Error when reading file {:?} from {:?}: {:?}",
        file_name, package_locator, cause
    )]
    PackagedFileError {
        /// Name of the file
        file_name: String,
        /// Package that we're trying to read
        package_locator: PackageLocator,
        #[fail(cause)]
        cause: ClonableError,
    },

    /// The client requested an empty response if the asset wasn't cached, and the asset is indeed
    /// not cached
    #[allow(dead_code)]
    #[fail(display = "Requested asset needs to be loaded ephemerally but is not yet cached")]
    UncachedEphemeral {
        /// Name of the file
        file_name: String,
        /// Package that we want to read
        package_locator: PackageLocator,
    },
}
