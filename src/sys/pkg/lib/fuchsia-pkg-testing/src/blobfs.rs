// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fake and mock implementation of blobfs for blobfs::Client.

use {
    fidl::endpoints::RequestStream as _,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryProxy, DirectoryRequest, DirectoryRequestStream, FileObject,
        FileRequest, FileRequestStream, NodeInfo,
    },
    fuchsia_hash::Hash,
    fuchsia_zircon::{self as zx, AsHandleRef as _, Status},
    futures::StreamExt as _,
    std::{cmp::min, convert::TryInto as _, fs::File},
    tempfile::TempDir,
};

/// A fake blobfs backed by temporary storage.
/// The name of the blob file is not guaranteed to match the merkle root of the content.
/// Be aware that this implementation does not send USER_0 signal, so `has_blob()` will always
/// return false.
pub struct Fake {
    root: TempDir,
}

impl Fake {
    /// Creates a new fake blobfs and client.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn new() -> (Self, blobfs::Client) {
        let fake = Self { root: TempDir::new().unwrap() };
        let blobfs = blobfs::Client::new(fake.root_proxy());
        (fake, blobfs)
    }

    /// Add a new blob to fake blobfs.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn add_blob(&self, hash: Hash, data: impl AsRef<[u8]>) {
        std::fs::write(self.root.path().join(hash.to_string()), data).unwrap();
    }

    fn root_proxy(&self) -> DirectoryProxy {
        DirectoryProxy::new(
            fuchsia_async::Channel::from_channel(
                fdio::transfer_fd(File::open(self.root.path()).unwrap()).unwrap().into(),
            )
            .unwrap(),
        )
    }
}

/// A testing server implementation of /blob.
///
/// Mock does not handle requests until instructed to do so.
pub struct Mock {
    stream: DirectoryRequestStream,
}

impl Mock {
    /// Creates a new mock blobfs and client.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn new() -> (Self, blobfs::Client) {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<DirectoryMarker>().unwrap();

        (Self { stream }, blobfs::Client::new(proxy))
    }

    /// Consume the next directory request, verifying it is intended to read the blob identified
    /// by `merkle`.  Returns a `MockBlob` representing the open blob file.
    ///
    /// # Panics
    ///
    /// Panics on error or assertion violation (unexpected requests or a mismatched open call)
    pub async fn expect_open_blob(&mut self, merkle: Hash) -> MockBlob {
        match self.stream.next().await {
            Some(Ok(DirectoryRequest::Open {
                flags: _,
                mode: _,
                path,
                object,
                control_handle: _,
            })) => {
                assert_eq!(path, merkle.to_string());

                let stream = object.into_stream().unwrap().cast_stream();
                MockBlob { stream }
            }
            other => panic!("unexpected request: {:?}", other),
        }
    }

    /// Asserts that the request stream closes without any further requests.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn expect_done(mut self) {
        match self.stream.next().await {
            None => {}
            Some(request) => panic!("unexpected request: {:?}", request),
        }
    }
}

/// A testing server implementation of an open /blob/<merkle> file.
///
/// MockBlob does not send the OnOpen event or handle requests until instructed to do so.
pub struct MockBlob {
    stream: FileRequestStream,
}

impl MockBlob {
    fn send_on_open_with_readable(&mut self, status: Status) {
        // Send USER_0 signal to indicate that the blob is available.
        let event = fidl::Event::create().unwrap();
        event.signal_handle(zx::Signals::NONE, zx::Signals::USER_0).unwrap();

        let mut info = NodeInfo::File(FileObject { event: Some(event), stream: None });
        let () =
            self.stream.control_handle().send_on_open_(status.into_raw(), Some(&mut info)).unwrap();
    }

    async fn handle_read(&mut self, data: &[u8]) -> usize {
        match self.stream.next().await {
            Some(Ok(FileRequest::Read { count, responder })) => {
                let count = min(count.try_into().unwrap(), data.len());
                responder.send(Status::OK.into_raw(), &data[..count]).unwrap();
                return count;
            }
            other => panic!("unexpected request: {:?}", other),
        }
    }

    /// Succeeds the open request, then verifies the blob is immediately closed (possibly after
    /// handling a single Close request).
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn expect_close(mut self) {
        self.send_on_open_with_readable(Status::OK);

        match self.stream.next().await {
            None => {}
            Some(Ok(FileRequest::Close { responder })) => {
                let _ = responder.send(Status::OK.into_raw());
            }
            Some(other) => panic!("unexpected request: {:?}", other),
        }
    }

    /// Succeeds the open request, then handle read request with the given blob data.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn expect_read(mut self, blob: &[u8]) {
        self.send_on_open_with_readable(Status::OK);

        let mut rest = blob;
        while !rest.is_empty() {
            let count = self.handle_read(rest).await;
            rest = &rest[count..];
        }

        // Handle one extra request with empty buffer to signal EOF.
        self.handle_read(rest).await;

        match self.stream.next().await {
            None => {}
            Some(Ok(FileRequest::Close { responder })) => {
                let _ = responder.send(Status::OK.into_raw());
            }
            Some(other) => panic!("unexpected request: {:?}", other),
        }
    }
}
