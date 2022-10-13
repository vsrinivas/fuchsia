// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Context as _,
    fidl_fuchsia_pkg::PackageIndexIteratorMarker,
    fuchsia_url::{PinnedAbsolutePackageUrl, UnpinnedAbsolutePackageUrl},
    tracing::warn,
};

/// Load the list of cache_packages from fuchsia.pkg/PackageCache.CachePackageIndex.
/// If any error is encountered during loading, an empty list will be returned.
pub async fn from_proxy(
    pkg_cache: &fidl_fuchsia_pkg::PackageCacheProxy,
) -> system_image::CachePackages {
    from_proxy_impl(pkg_cache).await.unwrap_or_else(|e| {
        warn!("Error loading cache packages manifest, using empty manifest. {:#}", e);
        system_image::CachePackages::from_entries(vec![])
    })
}

async fn from_proxy_impl(
    pkg_cache: &fidl_fuchsia_pkg::PackageCacheProxy,
) -> Result<system_image::CachePackages, anyhow::Error> {
    let (pkg_iterator, server_end) =
        fidl::endpoints::create_proxy::<PackageIndexIteratorMarker>().context("creating proxy")?;
    let () =
        pkg_cache.cache_package_index(server_end).context("calling cache_package_index fidl")?;

    let mut entries = Vec::with_capacity(256);
    let mut chunk = pkg_iterator.next().await.context("getting first iterator batch")?;
    while !chunk.is_empty() {
        for entry in chunk {
            let pkg_url =
                UnpinnedAbsolutePackageUrl::parse(&entry.package_url.url).context("parsing url")?;
            let blob_id = fidl_fuchsia_pkg_ext::BlobId::from(entry.meta_far_blob_id);
            entries.push(PinnedAbsolutePackageUrl::from_unpinned(pkg_url, blob_id.into()));
        }
        chunk = pkg_iterator.next().await.context("getting next iterator batch")?;
    }
    entries.shrink_to_fit();

    Ok(system_image::CachePackages::from_entries(entries))
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_pkg::{
            self as fpkg, PackageCacheMarker, PackageCacheRequest, PackageCacheRequestStream,
            PackageIndexEntry, PackageIndexIteratorRequest, PackageIndexIteratorRequestStream,
        },
        fidl_fuchsia_pkg_ext::BlobId,
        fuchsia_async as fasync,
        futures::prelude::*,
        std::sync::Arc,
    };

    // The actual pkg-cache will fit as many items per chunk as possible.  Intentionally choose a
    // small, fixed value here to verify the BasePackageIndex behavior with multiple chunks without
    // having to actually send hundreds of entries in these tests.
    const PACKAGE_INDEX_CHUNK_SIZE: u32 = 30;

    struct MockPackageCacheService {
        cache_packages: Arc<Vec<(UnpinnedAbsolutePackageUrl, BlobId)>>,
    }

    impl MockPackageCacheService {
        fn new_with_cache_packages(
            cache_packages: Arc<Vec<(UnpinnedAbsolutePackageUrl, BlobId)>>,
        ) -> Self {
            Self { cache_packages }
        }

        async fn run_service(self, mut stream: PackageCacheRequestStream) {
            while let Some(req) = stream.try_next().await.unwrap() {
                match req {
                    PackageCacheRequest::CachePackageIndex { iterator, control_handle: _ } => {
                        let iterator = iterator.into_stream().unwrap();
                        self.serve_package_iterator(iterator);
                    }
                    _ => panic!("unexpected PackageCache request: {:?}", req),
                }
            }
        }

        fn serve_package_iterator(&self, mut stream: PackageIndexIteratorRequestStream) {
            let mut packages = self
                .cache_packages
                .iter()
                .map(|(url, hash)| PackageIndexEntry {
                    package_url: fpkg::PackageUrl { url: url.to_string() },
                    meta_far_blob_id: BlobId::from(hash.clone()).into(),
                })
                .collect::<Vec<PackageIndexEntry>>();

            fasync::Task::spawn(async move {
                let mut iter = packages.iter_mut();
                while let Some(request) = stream.try_next().await.unwrap() {
                    let PackageIndexIteratorRequest::Next { responder } = request;
                    responder
                        .send(&mut iter.by_ref().take(PACKAGE_INDEX_CHUNK_SIZE as usize))
                        .unwrap();
                }
            })
            .detach();
        }
    }

    async fn spawn_pkg_cache(
        cache_package_index: Vec<(UnpinnedAbsolutePackageUrl, BlobId)>,
    ) -> fidl_fuchsia_pkg::PackageCacheProxy {
        let (client, request_stream) = create_proxy_and_stream::<PackageCacheMarker>().unwrap();
        let cache = MockPackageCacheService::new_with_cache_packages(Arc::new(cache_package_index));
        fasync::Task::spawn(cache.run_service(request_stream)).detach();
        client
    }

    #[fasync::run_singlethreaded(test)]
    async fn error_yields_empty() {
        let (client, _) = fidl::endpoints::create_proxy::<PackageCacheMarker>().unwrap();
        let cache_packages = from_proxy(&client).await;
        assert_eq!(cache_packages, system_image::CachePackages::from_entries(vec![]));
    }

    #[fasync::run_singlethreaded(test)]
    async fn empty_iterator() {
        let client = spawn_pkg_cache(vec![]).await;
        let cache_packages = from_proxy(&client).await;
        assert_eq!(cache_packages, system_image::CachePackages::from_entries(vec![]));
    }

    #[fasync::run_singlethreaded(test)]
    async fn variant_does_not_default_to_zero() {
        let client = spawn_pkg_cache(vec![(
            "fuchsia-pkg://fuchsia.com/no-variant".parse().unwrap(),
            BlobId::from([0; 32]),
        )])
        .await;
        let cache_packages = from_proxy(&client).await;
        assert_eq!(
            cache_packages,
            system_image::CachePackages::from_entries(vec![PinnedAbsolutePackageUrl::new(
                "fuchsia-pkg://fuchsia.com".parse().unwrap(),
                "no-variant".parse().unwrap(),
                None,
                [0; 32].into()
            ),])
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn set_variant_passes_through() {
        let client = spawn_pkg_cache(vec![(
            "fuchsia-pkg://fuchsia.com/has-variant/5".parse().unwrap(),
            BlobId::from([0; 32]),
        )])
        .await;
        let cache_packages = from_proxy(&client).await;
        assert_eq!(
            cache_packages,
            system_image::CachePackages::from_entries(vec![PinnedAbsolutePackageUrl::new(
                "fuchsia-pkg://fuchsia.com".parse().unwrap(),
                "has-variant".parse().unwrap(),
                Some("5".parse().unwrap()),
                [0; 32].into()
            ),])
        );
    }

    // Generate an index with n unique entries.
    fn index_with_n_entries(n: u32) -> Vec<(UnpinnedAbsolutePackageUrl, BlobId)> {
        let mut cache = vec![];
        for i in 0..n {
            let pkg_url = format!("fuchsia-pkg://fuchsia.com/{}", i)
                .parse::<UnpinnedAbsolutePackageUrl>()
                .unwrap();
            let blob_id = format!("{:064}", i).parse::<BlobId>().unwrap();
            cache.push((pkg_url, blob_id));
        }
        cache
    }

    #[fasync::run_singlethreaded(test)]
    async fn chunk_size_boundary() {
        let package_counts = [
            PACKAGE_INDEX_CHUNK_SIZE - 1,
            PACKAGE_INDEX_CHUNK_SIZE,
            PACKAGE_INDEX_CHUNK_SIZE + 1,
            2 * PACKAGE_INDEX_CHUNK_SIZE + 1,
        ];
        for count in package_counts.iter() {
            let expected_packages = index_with_n_entries(*count);
            let client = spawn_pkg_cache(expected_packages.clone()).await;

            let cache_packages = from_proxy(&client).await;

            let expected_packages = expected_packages
                .into_iter()
                .map(|(url, blob)| PinnedAbsolutePackageUrl::from_unpinned(url, blob.into()))
                .collect();
            assert_eq!(
                cache_packages,
                system_image::CachePackages::from_entries(expected_packages)
            );
        }
    }
}
