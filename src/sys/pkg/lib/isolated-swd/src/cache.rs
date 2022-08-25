// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Context, Error},
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_io as fio,
    fidl_fuchsia_update::{CommitStatusProviderRequest, CommitStatusProviderRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::{
        client::{App, AppBuilder},
        server::{NestedEnvironment, ServiceFs, ServiceObj},
    },
    fuchsia_syslog::fx_log_warn,
    fuchsia_zircon::{self as zx, HandleBased, Peered},
    futures::prelude::*,
    std::sync::Arc,
};

const CACHE_URL: &str =
    "fuchsia-pkg://fuchsia.com/isolated-swd-components#meta/pkg-cache-isolated.cmx";

/// Represents the sandboxed package cache.
pub struct Cache {
    pkg_cache: App,
    pkg_cache_directory: Arc<zx::Channel>,
    _env: NestedEnvironment,
}

impl Cache {
    /// Launch the package cache using the given blobfs.
    pub fn launch(blobfs: ClientEnd<fio::DirectoryMarker>) -> Result<Self, Error> {
        Self::launch_with_components(blobfs, CACHE_URL)
    }

    /// Launch the package cache. This is the same as `launch`, but the URL for the cache's
    /// manifest must be provided.
    fn launch_with_components(
        blobfs: ClientEnd<fio::DirectoryMarker>,
        cache_url: &str,
    ) -> Result<Self, Error> {
        let mut pkg_cache = AppBuilder::new(cache_url)
            .arg("--ignore-system-image")
            .add_handle_to_namespace("/blob".to_owned(), blobfs.into_handle());

        let mut fs: ServiceFs<ServiceObj<'_, ()>> = ServiceFs::new();
        fs.add_proxy_service::<fidl_fuchsia_net_name::LookupMarker, _>()
            .add_proxy_service::<fidl_fuchsia_posix_socket::ProviderMarker, _>()
            .add_proxy_service::<fidl_fuchsia_logger::LogSinkMarker, _>()
            .add_proxy_service::<fidl_fuchsia_tracing_provider::RegistryMarker, _>()
            // TODO(fxbug.dev/65383) investigate how isolated-ota should work with commits.
            // GC is traditionally blocked on the previous updated being committed. Here, we relax
            // this restriction because it does not make sense for isolated-swd.
            .add_fidl_service(move |stream| {
                fasync::Task::spawn(Self::serve_commit_status_provider(stream).unwrap_or_else(
                    |e| {
                        fx_log_warn!("error running CommitStatusProvider service: {:#}", anyhow!(e))
                    },
                ))
                .detach()
            });

        // We use a salt so the unit tests work as expected.
        let env = fs.create_salted_nested_environment("isolated-swd-env")?;
        fasync::Task::spawn(fs.collect()).detach();

        let directory = pkg_cache.directory_request().context("getting directory request")?.clone();
        let pkg_cache = pkg_cache.spawn(env.launcher()).context("launching package cache")?;

        Ok(Cache { pkg_cache, pkg_cache_directory: directory, _env: env })
    }

    pub fn directory_request(&self) -> Arc<fuchsia_zircon::Channel> {
        self.pkg_cache_directory.clone()
    }

    pub fn package_cache_proxy(&self) -> Result<fidl_fuchsia_pkg::PackageCacheProxy, Error> {
        self.pkg_cache
            .connect_to_protocol::<fidl_fuchsia_pkg::PackageCacheMarker>()
            .context("connecting to PackageCache service")
    }

    /// Serve a `CommitStatusProvider` that always says the system is committed.
    async fn serve_commit_status_provider(
        mut stream: CommitStatusProviderRequestStream,
    ) -> Result<(), Error> {
        let (p0, p1) = zx::EventPair::create().context("while creating event pair")?;
        let () = p0
            .signal_peer(zx::Signals::NONE, zx::Signals::USER_0)
            .context("while signalling peer")?;

        while let Some(request) = stream.try_next().await.context("while obtaining request")? {
            let CommitStatusProviderRequest::IsCurrentSystemCommitted { responder, .. } = request;
            let p1_clone =
                p1.duplicate_handle(zx::Rights::BASIC).context("while duplicating event pair")?;
            let () = responder.send(p1_clone).context("while sending event pair")?;
        }

        Ok(())
    }
}

pub mod for_tests {
    use super::*;

    /// This wraps the `Cache` to reduce test boilerplate.
    pub struct CacheForTest {
        pub blobfs: blobfs_ramdisk::BlobfsRamdisk,
        pub cache: Arc<Cache>,
    }

    impl CacheForTest {
        #[cfg(test)]
        /// Variant of `new_with_component` for use by isolated-swd tests.
        pub fn new() -> Result<Self, Error> {
            Self::new_with_component(
                "fuchsia-pkg://fuchsia.com/isolated-swd-tests#meta/pkg-cache-isolated.cmx",
            )
        }

        /// Create a new `Cache` and backing blobfs.
        pub fn new_with_component(url: &str) -> Result<Self, Error> {
            let blobfs = blobfs_ramdisk::BlobfsRamdisk::start().context("starting blobfs")?;
            let cache = Cache::launch_with_components(
                blobfs.root_dir_handle().context("blobfs handle")?,
                url,
            )
            .context("launching cache")?;

            Ok(CacheForTest { blobfs, cache: Arc::new(cache) })
        }
    }
}

#[cfg(test)]
pub mod tests {
    use {super::for_tests::CacheForTest, super::*};

    #[fasync::run_singlethreaded(test)]
    pub async fn test_cache_handles_sync() {
        let cache = CacheForTest::new().expect("launching cache");

        let proxy = cache.cache.package_cache_proxy().unwrap();

        assert_eq!(proxy.sync().await.unwrap(), Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    pub async fn test_cache_handles_gc() {
        let cache = CacheForTest::new().expect("launching cache");

        let proxy = cache
            .cache
            .pkg_cache
            .connect_to_protocol::<fidl_fuchsia_space::ManagerMarker>()
            .expect("connecting to space manager service");

        assert_eq!(proxy.gc().await.unwrap(), Ok(()));
    }
}
