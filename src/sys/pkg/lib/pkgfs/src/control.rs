// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Typesafe wrappers around the /pkgfs/ctl filesystem.

use {
    fidl_fuchsia_io::{DirectoryMarker, DirectoryProxy, DirectoryRequestStream},
    fuchsia_zircon::Status,
    thiserror::Error,
};

/// An error encountered while garbage collecting blobs
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum GcError {
    #[error("while sending request: {}", _0)]
    Fidl(fidl::Error),

    #[error("unlink failed with status: {}", _0)]
    UnlinkError(Status),
}

/// An error encountered while syncing
#[derive(Debug, Error)]
#[allow(missing_docs)]
pub enum SyncError {
    #[error("while sending request: {}", _0)]
    Fidl(fidl::Error),

    #[error("sync failed with status: {}", _0)]
    SyncError(Status),
}

/// An open handle to /pkgfs/ctl
#[derive(Debug, Clone)]
pub struct Client {
    proxy: DirectoryProxy,
}

impl Client {
    /// Returns an client connected to pkgfs from the current component's namespace
    pub fn open_from_namespace() -> Result<Self, io_util::node::OpenError> {
        let proxy = io_util::directory::open_in_namespace(
            "/pkgfs/ctl",
            fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
        )?;
        Ok(Client { proxy })
    }

    /// Returns an client connected to pkgfs from the given pkgfs root dir.
    pub fn open_from_pkgfs_root(pkgfs: &DirectoryProxy) -> Result<Self, io_util::node::OpenError> {
        Ok(Client {
            proxy: io_util::directory::open_directory_no_describe(
                pkgfs,
                "ctl",
                fidl_fuchsia_io::OPEN_RIGHT_READABLE | fidl_fuchsia_io::OPEN_RIGHT_WRITABLE,
            )?,
        })
    }

    /// Creates a new client backed by the returned request stream. This constructor should not be
    /// used outside of tests.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn new_test() -> (Self, DirectoryRequestStream) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DirectoryMarker>().unwrap();

        (Self { proxy }, stream)
    }

    /// Performs a garbage collection
    pub async fn gc(&self) -> Result<(), GcError> {
        match self.proxy.unlink("garbage").await.map(Status::from_raw) {
            Ok(Status::OK) => Ok(()),
            Ok(status) => Err(GcError::UnlinkError(status)),
            Err(err) => Err(GcError::Fidl(err)),
        }
    }

    /// Performs a sync
    pub async fn sync(&self) -> Result<(), SyncError> {
        match self.proxy.sync().await.map(Status::from_raw) {
            Ok(Status::OK) => Ok(()),
            Ok(status) => Err(SyncError::SyncError(status)),
            Err(err) => Err(SyncError::Fidl(err)),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_io::{DirectoryMarker, DirectoryRequest},
        fuchsia_async as fasync,
        futures_util::stream::TryStreamExt,
        matches::assert_matches,
    };

    #[fasync::run_singlethreaded(test)]
    async fn gc() {
        let (proxy, mut stream) = create_proxy_and_stream::<DirectoryMarker>().unwrap();
        fasync::Task::spawn(async move {
            match stream.try_next().await.unwrap().unwrap() {
                DirectoryRequest::Unlink { path, responder } => {
                    assert_eq!(path, "garbage");
                    responder.send(Status::OK.into_raw()).unwrap();
                }
                other => panic!("unexpected request: {:?}", other),
            }
        })
        .detach();

        assert_matches!(Client { proxy }.gc().await, Ok(()));
    }

    #[fasync::run_singlethreaded(test)]
    async fn sync() {
        let (proxy, mut stream) = create_proxy_and_stream::<DirectoryMarker>().unwrap();
        fasync::Task::spawn(async move {
            match stream.try_next().await.unwrap().unwrap() {
                DirectoryRequest::Sync { responder } => {
                    responder.send(Status::OK.into_raw()).unwrap();
                }
                other => panic!("unexpected request: {:?}", other),
            }
        })
        .detach();

        assert_matches!(Client { proxy }.sync().await, Ok(()));
    }
}
