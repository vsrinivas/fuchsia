// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::Error;
use fidl::endpoints::{ClientEnd, RequestStream, ServerEnd};
use fidl_fuchsia_io::{self, DirectoryMarker, DirectoryProxy};
use fidl_fuchsia_pkg::{BlobFetcherMarker, PackageCacheRequest, PackageCacheRequestStream};
use fidl_fuchsia_pkg_ext::BlobId;
use fuchsia_async as fasync;
use fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn};
use fuchsia_zircon::Status;
use futures::prelude::*;

pub async fn serve(pkgfs: DirectoryProxy, chan: fasync::Channel) -> Result<(), Error> {
    let mut stream = PackageCacheRequestStream::from_channel(chan);

    while let Some(event) = await!(stream.try_next())? {
        match event {
            PackageCacheRequest::Fetch {
                meta_far_blob_id,
                meta_far_length,
                selectors,
                fetcher,
                dir,
                responder,
            } => {
                let status = await!(fetch(
                    &pkgfs,
                    meta_far_blob_id.into(),
                    meta_far_length,
                    selectors,
                    fetcher,
                    dir
                ));
                responder.send(Status::from(status).into_raw())?;
            }
            PackageCacheRequest::Open {
                meta_far_blob_id,
                selectors,
                dir,
                responder,
            } => {
                let status = await!(open(&pkgfs, BlobId::from(meta_far_blob_id), selectors, dir));
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
async fn fetch<'a>(
    pkgfs: &'a DirectoryProxy, meta_far_blob_id: BlobId, _meta_far_length: u64,
    selectors: Vec<String>, _fetcher: ClientEnd<BlobFetcherMarker>,
    dir_request: Option<ServerEnd<DirectoryMarker>>,
) -> Result<(), Status> {
    fx_log_info!(
        "fetching {:?} with the selectors {:?}",
        meta_far_blob_id,
        selectors
    );

    if let Some(dir_request) = dir_request {
        await!(open(
            pkgfs,
            meta_far_blob_id,
            selectors.clone(),
            dir_request
        ))?;

        Ok(())
    } else {
        Err(Status::NOT_SUPPORTED)
    }
}

/// Open a package directory.
async fn open<'a>(
    pkgfs: &'a DirectoryProxy, meta_far_blob_id: BlobId, selectors: Vec<String>,
    dir_request: ServerEnd<DirectoryMarker>,
) -> Result<(), Status> {
    fx_log_info!(
        "opening {:?} with the selectors {:?}",
        meta_far_blob_id,
        selectors
    );

    // FIXME: need to implement selectors.
    if !selectors.is_empty() {
        fx_log_warn!("resolve does not support selectors yet");
    }

    // FIXME: this is a bit of a hack but there isn't a formal way to convert a Directory request
    // into a Node request.
    let node_request = ServerEnd::new(dir_request.into_channel());

    let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY;
    pkgfs
        .open(flags, 0, &meta_far_blob_id.to_string(), node_request)
        .map_err(|err| {
            fx_log_err!("error opening {}: {:?}", meta_far_blob_id, err);
            Status::INTERNAL
        })?;

    Ok(())
}
