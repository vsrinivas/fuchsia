// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::DirectoryMarker,
    fidl_fuchsia_pkg::{NeededBlobsMarker, PackageCacheRequest, PackageCacheRequestStream},
    fidl_fuchsia_pkg_ext::{BlobId, BlobInfo},
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_trace as trace,
    fuchsia_zircon::Status,
    futures::prelude::*,
};

pub async fn serve(
    pkgfs_versions: pkgfs::versions::Client,
    mut stream: PackageCacheRequestStream,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            PackageCacheRequest::Get { meta_far_blob, selectors, needed_blobs, dir, responder } => {
                let meta_far_blob: BlobInfo = meta_far_blob.into();
                trace::duration_begin!("app", "cache_get",
                    "meta_far_blob_id" => meta_far_blob.blob_id.to_string().as_str());
                let status =
                    get(&pkgfs_versions, meta_far_blob, selectors, needed_blobs, dir).await;
                trace::duration_end!("app", "cache_get",
                    "status" => Status::from(status).to_string().as_str());
                responder.send(Status::from(status).into_raw())?;
            }
            PackageCacheRequest::Open { meta_far_blob_id, selectors, dir, responder } => {
                let meta_far_blob_id: BlobId = meta_far_blob_id.into();
                trace::duration_begin!("app", "cache_open",
                    "meta_far_blob_id" => meta_far_blob_id.to_string().as_str());
                let status = open(&pkgfs_versions, meta_far_blob_id, selectors, dir).await;
                trace::duration_end!("app", "cache_open",
                    "status" => Status::from(status).to_string().as_str());
                responder.send(Status::from(status).into_raw())?;
            }
        }
    }

    Ok(())
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
) -> Result<(), Status> {
    fx_log_info!("fetching {:?} with the selectors {:?}", meta_far_blob, selectors);

    if let Some(dir_request) = dir_request {
        open(pkgfs_versions, meta_far_blob.blob_id, selectors.clone(), dir_request).await?;

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
) -> Result<(), Status> {
    // FIXME: need to implement selectors.
    if !selectors.is_empty() {
        fx_log_warn!("resolve does not support selectors yet");
    }

    match pkgfs_versions.open_package(meta_far_blob_id.into(), Some(dir_request)).await {
        Ok(_) => Ok(()),
        Err(pkgfs::versions::PackageOpenError::NotFound) => Err(Status::NOT_FOUND),
        Err(err) => {
            fx_log_err!("error opening {}: {:?}", meta_far_blob_id, err);
            Err(Status::INTERNAL)
        }
    }
}
