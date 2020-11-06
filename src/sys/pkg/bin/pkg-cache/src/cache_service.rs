// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Error},
    cobalt_sw_delivery_registry as metrics,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_pkg::{
        NeededBlobsMarker, PackageCacheRequest, PackageCacheRequestStream, PackageIndexEntry,
        PackageIndexIteratorRequest, PackageIndexIteratorRequestStream, PackageUrl,
    },
    fidl_fuchsia_pkg_ext::{BlobId, BlobInfo},
    fuchsia_cobalt::CobaltSender,
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_trace as trace,
    fuchsia_zircon::{sys::ZX_CHANNEL_MAX_MSG_BYTES, Status},
    futures::prelude::*,
    std::sync::Arc,
    system_image::StaticPackages,
};

// sizeof(TransactionHeader) + sizeof(VectorHeader)
const FIDL_VEC_RESPONSE_OVERHEAD_BYTES: usize = 32;

pub async fn serve(
    pkgfs_versions: pkgfs::versions::Client,
    pkgfs_ctl: pkgfs::control::Client,
    static_packages: Arc<StaticPackages>,
    stream: PackageCacheRequestStream,
    cobalt_sender: CobaltSender,
) -> Result<(), Error> {
    stream
        .map_err(anyhow::Error::new)
        .try_for_each_concurrent(None, |event| async {
            let cobalt_sender = cobalt_sender.clone();
            match event {
                PackageCacheRequest::Get {
                    meta_far_blob,
                    selectors,
                    needed_blobs,
                    dir,
                    responder,
                } => {
                    let meta_far_blob: BlobInfo = meta_far_blob.into();
                    trace::duration_begin!("app", "cache_get",
                        "meta_far_blob_id" => meta_far_blob.blob_id.to_string().as_str()
                    );
                    let response = get(
                        &pkgfs_versions,
                        meta_far_blob,
                        selectors,
                        needed_blobs,
                        dir,
                        cobalt_sender,
                    )
                    .await;
                    trace::duration_end!("app", "cache_get",
                        "status" => Status::from(response).to_string().as_str()
                    );
                    responder.send(&mut response.map_err(|status| status.into_raw()))?;
                }
                PackageCacheRequest::Open { meta_far_blob_id, selectors, dir, responder } => {
                    let meta_far_blob_id: BlobId = meta_far_blob_id.into();
                    trace::duration_begin!("app", "cache_open",
                        "meta_far_blob_id" => meta_far_blob_id.to_string().as_str()
                    );
                    let response =
                        open(&pkgfs_versions, meta_far_blob_id, selectors, dir, cobalt_sender)
                            .await;
                    trace::duration_end!("app", "cache_open",
                        "status" => Status::from(response).to_string().as_str()
                    );
                    responder.send(&mut response.map_err(|status| status.into_raw()))?;
                }
                PackageCacheRequest::BasePackageIndex { iterator, control_handle: _ } => {
                    let stream = iterator.into_stream()?;
                    serve_base_package_index(Arc::clone(&static_packages), stream).await;
                }
                PackageCacheRequest::Sync { responder } => {
                    responder.send(&mut pkgfs_ctl.sync().await.map_err(|e| {
                        fx_log_err!("error syncing /pkgfs/ctl: {:#}", anyhow!(e));
                        Status::INTERNAL.into_raw()
                    }))?;
                }
            }

            Ok(())
        })
        .await
}

/// Fetch a package, and optionally mount it.
///
/// TODO: implement this method. This stub can't simply proxy to amber for now, since it doesn't
/// know the name of the package, and amber needs it to lookup the package in its TUF repo.
async fn get<'a>(
    pkgfs_versions: &'a pkgfs::versions::Client,
    meta_far_blob: BlobInfo,
    selectors: Vec<String>,
    _needed_blobs: ServerEnd<NeededBlobsMarker>,
    dir_request: Option<ServerEnd<DirectoryMarker>>,
    cobalt_sender: CobaltSender,
) -> Result<(), Status> {
    fx_log_info!("fetching {:?} with the selectors {:?}", meta_far_blob, selectors);

    if let Some(dir_request) = dir_request {
        open(pkgfs_versions, meta_far_blob.blob_id, selectors.clone(), dir_request, cobalt_sender)
            .await?;

        Ok(())
    } else {
        Err(Status::NOT_SUPPORTED)
    }
}

/// Open a package directory.
async fn open<'a>(
    pkgfs_versions: &'a pkgfs::versions::Client,
    meta_far_blob_id: BlobId,
    selectors: Vec<String>,
    dir_request: ServerEnd<DirectoryMarker>,
    mut cobalt_sender: CobaltSender,
) -> Result<(), Status> {
    // FIXME: need to implement selectors.
    if !selectors.is_empty() {
        fx_log_warn!("resolve does not support selectors yet");
    }

    let pkg =
        pkgfs_versions.open_package(&meta_far_blob_id.into()).await.map_err(|err| match err {
            pkgfs::package::OpenError::NotFound => {
                cobalt_sender.log_event_count(
                    metrics::PKG_CACHE_OPEN_METRIC_ID,
                    metrics::PkgCacheOpenMetricDimensionResult::NotFound,
                    0,
                    1,
                );
                Status::NOT_FOUND
            }
            err => {
                cobalt_sender.log_event_count(
                    metrics::PKG_CACHE_OPEN_METRIC_ID,
                    metrics::PkgCacheOpenMetricDimensionResult::Io,
                    0,
                    1,
                );
                fx_log_err!("error opening {}: {:?}", meta_far_blob_id, err);
                Status::INTERNAL
            }
        })?;

    pkg.reopen(dir_request).map_err(|err| {
        fx_log_err!("error opening {}: {:#}", meta_far_blob_id, anyhow!(err));
        cobalt_sender.log_event_count(
            metrics::PKG_CACHE_OPEN_METRIC_ID,
            metrics::PkgCacheOpenMetricDimensionResult::Io,
            0,
            1,
        );
        Status::INTERNAL
    })?;

    cobalt_sender.log_event_count(
        metrics::PKG_CACHE_OPEN_METRIC_ID,
        metrics::PkgCacheOpenMetricDimensionResult::Success,
        0,
        1,
    );
    Ok(())
}

/// Serves the `PackageIndexIteratorRequest` with as many entries per request as will fit in a fidl
/// message.
async fn serve_base_package_index(
    static_packages: Arc<StaticPackages>,
    mut stream: PackageIndexIteratorRequestStream,
) {
    let mut package_entries = static_packages
        .contents()
        .map(|(path, hash)| PackageIndexEntry {
            package_url: PackageUrl { url: format!("fuchsia-pkg://fuchsia.com/{}", path.name()) },
            meta_far_blob_id: BlobId::from(hash.clone()).into(),
        })
        .collect::<Vec<PackageIndexEntry>>();

    let () = async move {
        let mut package_entries = &mut package_entries[..];

        while !package_entries.is_empty() {
            let mut bytes_used: usize = FIDL_VEC_RESPONSE_OVERHEAD_BYTES;
            let mut entry_count = 0;

            for entry in &*package_entries {
                bytes_used += measure_fuchsia_pkg::measure(&entry).num_bytes;
                if bytes_used > ZX_CHANNEL_MAX_MSG_BYTES as usize {
                    break;
                }
                entry_count += 1;
            }

            let (chunk, rest) = package_entries.split_at_mut(entry_count);
            package_entries = rest;

            if let Some(PackageIndexIteratorRequest::Next { responder }) = stream.try_next().await?
            {
                responder.send(&mut chunk.iter_mut())?;
            } else {
                return Ok(());
            }
        }

        // Send an empty payload if requested to indicate the iterator is completed.
        let mut eof = Vec::<PackageIndexEntry>::new();
        if let Some(PackageIndexIteratorRequest::Next { responder }) = stream.try_next().await? {
            responder.send(&mut eof.iter_mut())?;
        }
        Ok(())
    }
    .unwrap_or_else(|e: anyhow::Error| {
        fx_log_err!("error running BasePackageIndex protocol: {:#}", anyhow!(e))
    })
    .await;
}

#[cfg(test)]
mod tests {
    use {
        super::*, fidl_fuchsia_pkg::PackageIndexIteratorMarker, fuchsia_async::Task,
        fuchsia_hash::HashRangeFull, fuchsia_pkg::PackagePath,
    };

    const PACKAGE_INDEX_CHUNK_SIZE_MAX: usize = 818;
    const PACKAGE_INDEX_CHUNK_SIZE_MIN: usize = 372;

    #[test]
    fn verify_fidl_vec_response_overhead() {
        let vec_response_overhead = {
            use fidl::encoding::{TransactionHeader, TransactionMessage};

            let mut nop: Vec<()> = vec![];
            let mut msg =
                TransactionMessage { header: TransactionHeader::new(0, 0), body: &mut nop };

            fidl::encoding::with_tls_encoded(&mut msg, |bytes, _handles| {
                Result::<_, fidl::Error>::Ok(bytes.len())
            })
            .unwrap()
        };
        assert_eq!(vec_response_overhead, FIDL_VEC_RESPONSE_OVERHEAD_BYTES);
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn base_package_index_iterator_paginates_shortest_entries() {
        let names = ('a'..='z').cycle().map(|c| c.to_string());
        let paths = names.map(|name| PackagePath::from_name_and_variant(name, "0").unwrap());

        verify_base_package_iterator_pagination(paths, PACKAGE_INDEX_CHUNK_SIZE_MAX).await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn base_package_index_iterator_paginates_longest_entries() {
        let names = ('a'..='z')
            .map(|c| std::iter::repeat(c).take(PackagePath::MAX_NAME_BYTES).collect::<String>())
            .cycle();
        let paths = names.map(|name| PackagePath::from_name_and_variant(name, "0").unwrap());

        verify_base_package_iterator_pagination(paths, PACKAGE_INDEX_CHUNK_SIZE_MIN).await;
    }

    async fn verify_base_package_iterator_pagination(
        paths: impl Iterator<Item = PackagePath>,
        expected_chunk_size: usize,
    ) {
        let static_packages =
            paths.zip(HashRangeFull::default()).take(expected_chunk_size * 2).collect();
        let static_packages = Arc::new(StaticPackages::from_entries(static_packages));

        let (iter, stream) =
            fidl::endpoints::create_proxy_and_stream::<PackageIndexIteratorMarker>().unwrap();
        let task = Task::local(serve_base_package_index(static_packages, stream));

        let chunk = iter.next().await.unwrap();
        assert_eq!(chunk.len(), expected_chunk_size);

        let chunk = iter.next().await.unwrap();
        assert_eq!(chunk.len(), expected_chunk_size);

        let chunk = iter.next().await.unwrap();
        assert_eq!(chunk.len(), 0);

        let () = task.await;
    }
}
