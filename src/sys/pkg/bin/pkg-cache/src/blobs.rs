// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_hash::Hash;

#[derive(Debug)]
pub enum BlobKind {
    /// The blob should be interpreted as a package.
    Package,
    /// The blob should be interpreted as a content blob in a package.
    Data,
}

impl From<BlobKind> for pkgfs::install::BlobKind {
    fn from(x: BlobKind) -> Self {
        match x {
            BlobKind::Package => pkgfs::install::BlobKind::Package,
            BlobKind::Data => pkgfs::install::BlobKind::Data,
        }
    }
}

impl From<pkgfs::install::BlobKind> for BlobKind {
    fn from(x: pkgfs::install::BlobKind) -> Self {
        match x {
            pkgfs::install::BlobKind::Package => BlobKind::Package,
            pkgfs::install::BlobKind::Data => BlobKind::Data,
        }
    }
}

#[derive(Debug)]
pub enum OpenBlobSuccess {
    /// The blob is still needed.
    Needed(OpenBlob),

    /// The blob already exists and does not need to be written.
    AlreadyExists,
}

#[derive(thiserror::Error, Debug)]
pub enum OpenBlobError {
    #[error("the blob is in the process of being written")]
    ConcurrentWrite,

    #[error("while opening the blob for write")]
    Io(#[source] io_util::node::OpenError),
}

/// Opens the requested blob for write.
pub async fn open_blob(
    pkgfs_install: &pkgfs::install::Client,
    merkle: Hash,
    kind: BlobKind,
) -> Result<OpenBlobSuccess, OpenBlobError> {
    use pkgfs::install::BlobCreateError;

    let res = pkgfs_install.create_blob(merkle, kind.into()).await;

    match res {
        Ok((blob, closer)) => Ok(OpenBlobSuccess::Needed(OpenBlob { blob, closer })),
        Err(BlobCreateError::AlreadyExists) => Ok(OpenBlobSuccess::AlreadyExists),
        Err(BlobCreateError::ConcurrentWrite) => Err(OpenBlobError::ConcurrentWrite),
        Err(BlobCreateError::Io(err)) => Err(OpenBlobError::Io(err)),
    }
}

/// A blob opened for write.
#[derive(Debug)]
pub struct OpenBlob {
    pub blob: pkgfs::install::Blob<pkgfs::install::NeedsTruncate>,
    pub closer: pkgfs::install::BlobCloser,
}

impl OpenBlob {
    /// Creates a new OpenBlob backed by the returned file request stream.
    #[cfg(test)]
    pub fn new_test(kind: BlobKind) -> (Self, fidl_fuchsia_io::FileRequestStream) {
        let (blob, closer, stream) = pkgfs::install::Blob::new_test(kind.into());
        (Self { blob, closer }, stream)
    }
}

#[cfg(test)]
mod open_blob_tests {
    use {
        super::*,
        fidl::endpoints::RequestStream,
        fidl_fuchsia_io::{DirectoryRequest, DirectoryRequestStream, FileObject, NodeInfo},
        fuchsia_async::Task,
        fuchsia_zircon::Status,
        futures::prelude::*,
        matches::assert_matches,
    };

    /// Handles a single FIDL request on the provided stream, panicking if the received request is
    /// not the expected kind.
    macro_rules! serve_fidl_request {
        (
            $stream:expr, { $pat:pat => $handler:block, }
        ) => {
            match $stream.next().await.unwrap().unwrap() {
                $pat => $handler,
                req => panic!("unexpected request: {:?}", req),
            }
        };
    }

    async fn handle_open(
        stream: &mut DirectoryRequestStream,
        expected_path: &'static str,
        response: Status,
    ) {
        serve_fidl_request!(stream, {
            DirectoryRequest::Open{flags: _, mode: _, path, object, control_handle: _ } => {
                assert_eq!(path, expected_path);

                let file_stream = object.into_stream().unwrap();

                let mut info = NodeInfo::File(FileObject{ event: None, stream: None });
                let () = file_stream
                    .control_handle()
                    .send_on_open_(response.into_raw(), Some(&mut info))
                    .unwrap();
            },
        });
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn open_closed_proxy() {
        let (install, stream) = pkgfs::install::Client::new_test();
        drop(stream);

        let res = open_blob(&install, Hash::from([0; 32]), BlobKind::Package).await;
        assert_matches!(res, Err(OpenBlobError::Io(_)));
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn opens_needed_package_blob() {
        let (install, mut stream) = pkgfs::install::Client::new_test();
        let task = Task::spawn(async move {
            handle_open(
                &mut stream,
                "pkg/0000000000000000000000000000000000000000000000000000000000000000",
                Status::OK,
            )
            .await;
        });

        let res = open_blob(&install, Hash::from([0; 32]), BlobKind::Package).await;
        assert_matches!(res, Ok(OpenBlobSuccess::Needed(_)));
        task.await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn opens_needed_data_blob() {
        let (install, mut stream) = pkgfs::install::Client::new_test();
        let task = Task::spawn(async move {
            handle_open(
                &mut stream,
                "blob/4444444444444444444444444444444444444444444444444444444444444444",
                Status::OK,
            )
            .await;
        });

        let res = open_blob(&install, Hash::from([0x44; 32]), BlobKind::Data).await;
        assert_matches!(res, Ok(OpenBlobSuccess::Needed(_)));
        task.await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn handles_present_blobs() {
        let (install, mut stream) = pkgfs::install::Client::new_test();
        let task = Task::spawn(async move {
            handle_open(
                &mut stream,
                "pkg/0000000000000000000000000000000000000000000000000000000000000000",
                Status::ALREADY_EXISTS,
            )
            .await;
        });

        let res = open_blob(&install, Hash::from([0; 32]), BlobKind::Package).await;
        assert_matches!(res, Ok(OpenBlobSuccess::AlreadyExists));
        task.await;
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn fails_on_concurrent_writes() {
        let (install, mut stream) = pkgfs::install::Client::new_test();
        let task = Task::spawn(async move {
            handle_open(
                &mut stream,
                "pkg/0000000000000000000000000000000000000000000000000000000000000000",
                Status::ACCESS_DENIED,
            )
            .await;
        });

        let res = open_blob(&install, Hash::from([0; 32]), BlobKind::Package).await;
        assert_matches!(res, Err(OpenBlobError::ConcurrentWrite));
        task.await;
    }
}
