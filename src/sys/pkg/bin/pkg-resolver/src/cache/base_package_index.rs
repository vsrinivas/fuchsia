// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::cache::BasePackageIndex,
    anyhow::Error,
    fidl_fuchsia_pkg::{PackageCacheProxy, PackageIndexIteratorMarker},
    fidl_fuchsia_pkg_ext::BlobId,
    fuchsia_url::pkg_url::PkgUrl,
    std::collections::HashMap,
};

/// Loads all of the base packages and returns them in a hash map.
/// Unpinned base package resolution skips TUF and returns the directory
/// directly from pkg-cache.
pub async fn base_package_index_impl(cache: PackageCacheProxy) -> Result<BasePackageIndex, Error> {
    let (pkg_iterator, server_end) = fidl::endpoints::create_proxy::<PackageIndexIteratorMarker>()?;
    cache.base_package_index(server_end)?;

    let mut base_package_index_map = HashMap::with_capacity(256);
    let mut chunk = pkg_iterator.next().await?;
    while !chunk.is_empty() {
        for entry in chunk {
            let pkg_url = PkgUrl::parse(&entry.package_url.url)?;
            let blob_id = BlobId::from(entry.meta_far_blob_id);
            base_package_index_map.insert(pkg_url, blob_id);
        }
        chunk = pkg_iterator.next().await?;
    }
    Ok(base_package_index_map)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_pkg::{
            PackageCacheMarker, PackageCacheRequest, PackageCacheRequestStream, PackageIndexEntry,
            PackageIndexIteratorRequest, PackageIndexIteratorRequestStream, PackageUrl,
            PACKAGE_INDEX_CHUNK_SIZE,
        },
        fuchsia_async as fasync,
        fuchsia_syslog::fx_log_err,
        futures::prelude::*,
        std::sync::Arc,
    };

    struct MockPackageCacheService {
        base_packages: Arc<BasePackageIndex>,
    }

    impl MockPackageCacheService {
        fn new_with_base_packages(base_packages: Arc<BasePackageIndex>) -> Self {
            Self { base_packages: base_packages }
        }

        async fn run_service(self, mut stream: PackageCacheRequestStream) {
            while let Some(req) = stream.try_next().await.unwrap() {
                match req {
                    PackageCacheRequest::BasePackageIndex { iterator, control_handle: _ } => {
                        let iterator = iterator.into_stream().unwrap();
                        self.serve_package_iterator(iterator);
                    }
                    _ => panic!("unexpected PackageCache request: {:?}", req),
                }
            }
        }

        fn serve_package_iterator(&self, mut stream: PackageIndexIteratorRequestStream) {
            let mut packages = self
                .base_packages
                .iter()
                .map(|(path, hash)| PackageIndexEntry {
                    package_url: PackageUrl {
                        url: format!("fuchsia-pkg://fuchsia.com/{}", path.name()),
                    },
                    meta_far_blob_id: BlobId::from(hash.clone()).into(),
                })
                .collect::<Vec<PackageIndexEntry>>();

            fasync::Task::spawn(
                async move {
                    let mut iter = packages.iter_mut();
                    while let Some(request) = stream.try_next().await? {
                        let PackageIndexIteratorRequest::Next { responder } = request;

                        responder
                            .send(&mut iter.by_ref().take(PACKAGE_INDEX_CHUNK_SIZE as usize))?;
                    }
                    Ok(())
                }
                .unwrap_or_else(|e: fidl::Error| {
                    fx_log_err!("while serving package index iterator: {:?}", e)
                }),
            )
            .detach();
        }
    }

    async fn spawn_pkg_cache(base_package_index: BasePackageIndex) -> PackageCacheProxy {
        let (client, request_stream) = create_proxy_and_stream::<PackageCacheMarker>().unwrap();
        let cache = MockPackageCacheService::new_with_base_packages(Arc::new(base_package_index));
        fasync::Task::spawn(cache.run_service(request_stream)).detach();
        client
    }

    // Generate a BasePackageIndex with n unique entries.
    fn base_packages_with_entries(n: u32) -> BasePackageIndex {
        let mut base = BasePackageIndex::new();
        for i in 0..n {
            let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", i).parse::<PkgUrl>().unwrap();
            let blob_id = format!("{:064}", i).parse::<BlobId>().unwrap();
            base.insert(pkg_url, blob_id);
        }
        base
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_empty_base_packages() {
        let empty_packages = BasePackageIndex::new();
        let client = spawn_pkg_cache(empty_packages.clone()).await;
        let res = base_package_index_impl(client).await.unwrap();
        assert_eq!(res, empty_packages);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_chunk_size() {
        let base_packages = base_packages_with_entries(PACKAGE_INDEX_CHUNK_SIZE);
        let client = spawn_pkg_cache(base_packages.clone()).await;
        let res = base_package_index_impl(client).await.unwrap();
        assert_eq!(res, base_packages);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_chunk_size_minus_one() {
        let base_packages = base_packages_with_entries(PACKAGE_INDEX_CHUNK_SIZE - 1);
        let client = spawn_pkg_cache(base_packages.clone()).await;
        let res = base_package_index_impl(client).await.unwrap();
        assert_eq!(res, base_packages);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_chunk_size_plus_one() {
        let base_packages = base_packages_with_entries(PACKAGE_INDEX_CHUNK_SIZE + 1);
        let client = spawn_pkg_cache(base_packages.clone()).await;
        let res = base_package_index_impl(client).await.unwrap();
        assert_eq!(res, base_packages);
    }
    #[fasync::run_singlethreaded(test)]
    async fn test_double_chunk_size_plus_one() {
        let base_packages = base_packages_with_entries(2 * PACKAGE_INDEX_CHUNK_SIZE + 1);
        let client = spawn_pkg_cache(base_packages.clone()).await;
        let res = base_package_index_impl(client).await.unwrap();
        assert_eq!(res, base_packages);
    }
}
