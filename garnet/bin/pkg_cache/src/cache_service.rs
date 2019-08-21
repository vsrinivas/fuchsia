// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{DirectoryEvent, DirectoryMarker, DirectoryProxy},
    fidl_fuchsia_pkg::{NeededBlobsMarker, PackageCacheRequest, PackageCacheRequestStream},
    fidl_fuchsia_pkg_ext::{BlobId, BlobInfo},
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_zircon::Status,
    futures::prelude::*,
};

pub async fn serve(
    pkgfs: DirectoryProxy,
    mut stream: PackageCacheRequestStream,
) -> Result<(), Error> {
    while let Some(event) = stream.try_next().await? {
        match event {
            PackageCacheRequest::Get { meta_far_blob, selectors, needed_blobs, dir, responder } => {
                let status = get(&pkgfs, meta_far_blob.into(), selectors, needed_blobs, dir).await;
                responder.send(Status::from(status).into_raw())?;
            }
            PackageCacheRequest::Open { meta_far_blob_id, selectors, dir, responder } => {
                let status = open(&pkgfs, meta_far_blob_id.into(), selectors, dir).await;
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
        open(pkgfs, meta_far_blob.blob_id, selectors.clone(), dir_request).await?;

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

    // open the package using a local handle so we can intercept the NOT_FOUND case.
    let (dir, server_end) =
        fidl::endpoints::create_proxy::<DirectoryMarker>().map_err(|_| Status::INTERNAL)?;
    let flags = fidl_fuchsia_io::OPEN_RIGHT_READABLE
        | fidl_fuchsia_io::OPEN_FLAG_DIRECTORY
        | fidl_fuchsia_io::OPEN_FLAG_DESCRIBE;
    pkgfs
        .open(flags, 0, &meta_far_blob_id.to_string(), ServerEnd::new(server_end.into_channel()))
        .map_err(|err| {
            fx_log_err!("error opening {}: {:?}", meta_far_blob_id, err);
            Status::INTERNAL
        })?;

    // wait for the directory to open and report success.
    let mut events = dir.take_event_stream();
    let DirectoryEvent::OnOpen_ { s: status, info: _ } = events
        .next()
        .await
        .ok_or_else(|| {
            fx_log_err!("package dir event stream closed prematurely");
            Err(Status::INTERNAL)
        })?
        .map_err(|e| {
            fx_log_err!("failed to read package OnOpen event: {:?}", e);
            Err(Status::INTERNAL)
        })?;

    let () = Status::ok(status).map_err(|status| match status {
        Status::NOT_FOUND => Status::NOT_FOUND,
        status => {
            fx_log_err!("unexpected error opening package directory: {:?}", status);
            Status::INTERNAL
        }
    })?;

    // serve the directory on the client provided handle.
    let node_request = ServerEnd::new(dir_request.into_channel());
    dir.clone(fidl_fuchsia_io::CLONE_FLAG_SAME_RIGHTS, node_request).map_err(|err| {
        fx_log_err!("error cloning dir handle {}: {:?}", meta_far_blob_id, err);
        Status::INTERNAL
    })?;

    Ok(())
}
