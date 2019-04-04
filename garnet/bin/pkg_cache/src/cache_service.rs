// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl::endpoints::{RequestStream, ServerEnd};
use fidl_fuchsia_io::{self, DirectoryMarker, DirectoryProxy};
use fidl_fuchsia_pkg::{NeededBlobsMarker, PackageCacheRequest, PackageCacheRequestStream};
use fidl_fuchsia_pkg_ext::{BlobId, BlobInfo};
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn};
use fuchsia_zircon::Status;
use futures::prelude::*;

pub async fn serve(pkgfs: DirectoryProxy, chan: fasync::Channel) -> Result<(), Error> {
    let mut stream = PackageCacheRequestStream::from_channel(chan);

    while let Some(event) = await!(stream.try_next())? {
        match event {
            PackageCacheRequest::Get { meta_far_blob, selectors, needed_blobs, dir, responder } => {
                let status =
                    await!(get(&pkgfs, meta_far_blob.into(), selectors, needed_blobs, dir));
                responder.send(Status::from(status).into_raw())?;
            }
            PackageCacheRequest::Open { meta_far_blob_id, selectors, dir, responder } => {
                let status = await!(open(&pkgfs, meta_far_blob_id.into(), selectors, dir));
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
    pkgfs: &'a DirectoryProxy,
    meta_far_blob: BlobInfo,
    selectors: Vec<String>,
    _needed_blobs: ServerEnd<NeededBlobsMarker>,
    dir_request: Option<ServerEnd<DirectoryMarker>>,
) -> Result<(), Status> {
    fx_log_info!("fetching {:?} with the selectors {:?}", meta_far_blob, selectors);

    if let Some(dir_request) = dir_request {
        await!(open(pkgfs, meta_far_blob.blob_id, selectors.clone(), dir_request))?;

        Ok(())
    } else {
        Err(Status::NOT_SUPPORTED)
    }
}

/// Open a package directory.
async fn open<'a>(
    pkgfs: &'a DirectoryProxy,
    meta_far_blob_id: BlobId,
    selectors: Vec<String>,
    dir_request: ServerEnd<DirectoryMarker>,
) -> Result<(), Status> {
    // FIXME: need to implement selectors.
    if !selectors.is_empty() {
        fx_log_warn!("resolve does not support selectors yet");
    }

    // FIXME: this is a bit of a hack but there isn't a formal way to convert a Directory request
    // into a Node request.
    let node_request = ServerEnd::new(dir_request.into_channel());

    let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY;
    pkgfs.open(flags, 0, &meta_far_blob_id.to_string(), node_request).map_err(|err| {
        fx_log_err!("error opening {}: {:?}", meta_far_blob_id, err);
        Status::INTERNAL
    })?;

    Ok(())
}
