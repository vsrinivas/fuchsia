// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{Context, Error},
    fidl::endpoints::Proxy,
    fidl::endpoints::ServerEnd,
    std::sync::Arc,
};

/// Represents the sandboxed package cache.
pub struct Cache {
    pkg_cache_proxy: fidl_fuchsia_pkg::PackageCacheProxy,
    _space_manager_proxy: fidl_fuchsia_space::ManagerProxy,
    svc_dir_proxy: fidl_fuchsia_io::DirectoryProxy,
}

impl Cache {
    /// Construct a new `Cache` object with pre-created proxies to package cache, space manager, and
    /// a `directory_proxy_with_access_to_pkg_cache`. This last argument is expected to be a
    /// `DirectoryProxy` to a `/svc` directory which has fuchsia.pkg.PackageCache in its namespace.
    /// This is required because the CFv1-based `AppBuilder` which constructs pkg-resolver and
    /// system-updater on top of this `Cache` object needs a directory proxy to a svc directory, not
    /// a proxy to an individual service.
    //
    // TODO(fxbug.dev/104919): delete directory_proxy_with_access_to_pkg_cache once v1 components
    // no longer require it.
    pub fn new_with_proxies(
        pkg_cache_proxy: fidl_fuchsia_pkg::PackageCacheProxy,
        space_manager_proxy: fidl_fuchsia_space::ManagerProxy,
        directory_proxy_with_access_to_pkg_cache: fidl_fuchsia_io::DirectoryProxy,
    ) -> Result<Self, Error> {
        Ok(Self {
            pkg_cache_proxy,
            _space_manager_proxy: space_manager_proxy,
            svc_dir_proxy: directory_proxy_with_access_to_pkg_cache,
        })
    }

    /// Construct a new `Cache` object using capabilities available in the namespace of the component
    /// calling this function. Should be the default in production usage, as these capabilities
    /// should be statically routed (i.e. from `pkg-recovery.cml`).
    pub fn new() -> Result<Self, Error> {
        // TODO(https://fxbug.dev/110044): Remove WRITABLE when FIDL through /svc works without it.
        let svc_dir_proxy = fuchsia_fs::directory::open_in_namespace(
            "/svc",
            fidl_fuchsia_io::OpenFlags::RIGHT_READABLE | fidl_fuchsia_io::OpenFlags::RIGHT_WRITABLE,
        )
        .context("error opening svc directory")?;

        Ok(Self {
            pkg_cache_proxy: fuchsia_component::client::connect_to_protocol::<
                fidl_fuchsia_pkg::PackageCacheMarker,
            >()?,
            _space_manager_proxy: fuchsia_component::client::connect_to_protocol::<
                fidl_fuchsia_space::ManagerMarker,
            >()?,
            svc_dir_proxy,
        })
    }

    /// TODO(fxbug.dev/104919): delete once CFv2 migration is done
    fn clone_cache_proxy(
        cache_proxy: &fidl_fuchsia_io::DirectoryProxy,
    ) -> Result<fidl_fuchsia_io::DirectoryProxy, Error> {
        let (cache_clone, remote) =
            fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()?;
        cache_proxy.clone(
            fidl_fuchsia_io::OpenFlags::CLONE_SAME_RIGHTS,
            ServerEnd::from(remote.into_channel()),
        )?;
        Ok(cache_clone.into_proxy()?)
    }

    /// Get a proxy to an instance of fuchsia.pkg.PackageCache.
    pub fn package_cache_proxy(&self) -> Result<fidl_fuchsia_pkg::PackageCacheProxy, Error> {
        Ok(self.pkg_cache_proxy.clone())
    }

    /// Get access to a /svc directory with access to fuchsia.pkg.PackageCache. Required in order to
    /// use `AppBuilder` to construct v1 components with access to PackageCache.
    // TODO(fxbug.dev/104919): delete
    pub fn directory_request(
        &self,
    ) -> Result<Arc<fidl::endpoints::ClientEnd<fidl_fuchsia_io::DirectoryMarker>>, Error> {
        // TODO(https://fxbug.dev/108786): Use Proxy::into_client_end when available.
        Ok(std::sync::Arc::new(fidl::endpoints::ClientEnd::new(
            Self::clone_cache_proxy(&self.svc_dir_proxy)?
                .into_channel()
                .expect("proxy into channel")
                .into_zx_channel(),
        )))
    }
}

#[cfg(test)]
pub(crate) mod for_tests {
    use fuchsia_component_test::ChildRef;

    use {
        super::*,
        blobfs_ramdisk::BlobfsRamdisk,
        fidl_fuchsia_io as fio,
        fuchsia_component_test::{
            Capability, ChildOptions, RealmBuilder, RealmInstance, Ref, Route,
        },
        futures::prelude::*,
        std::sync::Arc,
        vfs::directory::entry::DirectoryEntry,
    };

    /// This wraps the `Cache` to reduce test boilerplate.
    pub struct CacheForTest {
        pub blobfs: blobfs_ramdisk::BlobfsRamdisk,
        pub cache: Arc<Cache>,
    }

    impl CacheForTest {
        pub async fn realm_setup(
            realm_builder: &RealmBuilder,
            blobfs: &BlobfsRamdisk,
        ) -> Result<ChildRef, Error> {
            let blobfs_proxy = blobfs.root_dir_proxy().context("getting root dir proxy").unwrap();
            let blobfs_vfs = vfs::remote::remote_dir(blobfs_proxy);

            let local_mocks = realm_builder
                .add_local_child(
                    "pkg_cache_blobfs_mock",
                    move |handles| {
                        let blobfs_clone = blobfs_vfs.clone();
                        let out_dir = vfs::pseudo_directory! {
                            "blob" => blobfs_clone,
                        };
                        let scope = vfs::execution_scope::ExecutionScope::new();
                        let () = out_dir.open(
                            scope.clone(),
                            fio::OpenFlags::RIGHT_READABLE
                                | fio::OpenFlags::RIGHT_WRITABLE
                                | fio::OpenFlags::RIGHT_EXECUTABLE,
                            0,
                            vfs::path::Path::dot(),
                            handles.outgoing_dir.into_channel().into(),
                        );
                        async move { Ok(scope.wait().await) }.boxed()
                    },
                    ChildOptions::new(),
                )
                .await
                .unwrap();

            let pkg_cache = realm_builder
                .add_child(
                    "pkg_cache",
                    "#meta/pkg-cache-ignore-system-image.cm",
                    ChildOptions::new(),
                )
                .await
                .unwrap();
            let system_update_committer = realm_builder
                .add_child(
                    "system-update-committer",
                    "#meta/fake-system-update-committer.cm",
                    ChildOptions::new(),
                )
                .await
                .unwrap();

            realm_builder
                .add_route(
                    Route::new()
                        .capability(
                            Capability::directory("blob-exec")
                                .path("/blob")
                                .rights(fio::RW_STAR_DIR | fio::Operations::EXECUTE),
                        )
                        .from(&local_mocks)
                        .to(&pkg_cache),
                )
                .await
                .unwrap();

            realm_builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name("fuchsia.logger.LogSink"))
                        .from(Ref::parent())
                        .to(&pkg_cache),
                )
                .await
                .unwrap();

            realm_builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name("fuchsia.pkg.PackageCache"))
                        .capability(Capability::protocol_by_name("fuchsia.pkg.RetainedPackages"))
                        .capability(Capability::protocol_by_name("fuchsia.space.Manager"))
                        .from(&pkg_cache)
                        .to(Ref::parent()),
                )
                .await
                .unwrap();

            realm_builder
                .add_route(
                    Route::new()
                        .capability(Capability::protocol_by_name(
                            "fuchsia.update.CommitStatusProvider",
                        ))
                        .from(&system_update_committer)
                        .to(&pkg_cache),
                )
                .await
                .unwrap();
            Ok(pkg_cache)
        }

        pub async fn new(
            realm_instance: &RealmInstance,
            blobfs: BlobfsRamdisk,
        ) -> Result<Self, Error> {
            let pkg_cache_proxy = realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<fidl_fuchsia_pkg::PackageCacheMarker>()
                .expect("connect to pkg cache");
            let space_manager_proxy = realm_instance
                .root
                .connect_to_protocol_at_exposed_dir::<fidl_fuchsia_space::ManagerMarker>()
                .expect("connect to space manager");

            let (cache_clone, remote) =
                fidl::endpoints::create_endpoints::<fidl_fuchsia_io::DirectoryMarker>()?;
            realm_instance.root.get_exposed_dir().clone(
                fidl_fuchsia_io::OpenFlags::CLONE_SAME_RIGHTS,
                ServerEnd::from(remote.into_channel()),
            )?;

            let cache = Cache::new_with_proxies(
                pkg_cache_proxy,
                space_manager_proxy,
                cache_clone.into_proxy()?,
            )
            .unwrap();

            Ok(CacheForTest { blobfs, cache: Arc::new(cache) })
        }
    }
}

#[cfg(test)]
mod tests {
    use super::for_tests::CacheForTest;
    use fuchsia_async as fasync;
    use fuchsia_component_test::RealmBuilder;

    #[fasync::run_singlethreaded(test)]
    pub async fn test_cache_handles_sync() {
        let realm_builder = RealmBuilder::new().await.unwrap();
        let blobfs = blobfs_ramdisk::BlobfsRamdisk::start().expect("starting blobfs");

        let _cache_ref =
            CacheForTest::realm_setup(&realm_builder, &blobfs).await.expect("setting up realm");
        let realm_instance = realm_builder.build().await.unwrap();
        let cache = CacheForTest::new(&realm_instance, blobfs).await.expect("launching cache");
        let proxy = cache.cache.package_cache_proxy().unwrap();

        assert_eq!(proxy.sync().await.unwrap(), Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    pub async fn test_cache_handles_gc() {
        let realm_builder = RealmBuilder::new().await.unwrap();
        let blobfs = blobfs_ramdisk::BlobfsRamdisk::start().expect("starting blobfs");
        let _cache_ref =
            CacheForTest::realm_setup(&realm_builder, &blobfs).await.expect("setting up realm");
        let realm_instance = realm_builder.build().await.unwrap();
        let cache = CacheForTest::new(&realm_instance, blobfs).await.expect("launching cache");
        let proxy = cache.cache._space_manager_proxy.clone();

        assert_eq!(proxy.gc().await.unwrap(), Ok(()));
    }
}
