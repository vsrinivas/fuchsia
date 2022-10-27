// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Mock implementation of blobfs for blobfs::Client.

use {
    fidl::{encoding::Decodable as _, endpoints::RequestStream as _},
    fidl_fuchsia_io as fio,
    fuchsia_hash::Hash,
    fuchsia_zircon::{self as zx, AsHandleRef as _, Status},
    futures::{Future, StreamExt as _, TryStreamExt as _},
    std::{cmp::min, collections::HashSet, convert::TryInto as _},
};

/// A testing server implementation of /blob.
///
/// Mock does not handle requests until instructed to do so.
pub struct Mock {
    pub(super) stream: fio::DirectoryRequestStream,
}

impl Mock {
    /// Consume the next directory request, verifying it is intended to read the blob identified
    /// by `merkle`.  Returns a `Blob` representing the open blob file.
    ///
    /// # Panics
    ///
    /// Panics on error or assertion violation (unexpected requests or a mismatched open call)
    pub async fn expect_open_blob(&mut self, merkle: Hash) -> Blob {
        match self.stream.next().await {
            Some(Ok(fio::DirectoryRequest::Open {
                flags,
                mode: _,
                path,
                object,
                control_handle: _,
            })) => {
                assert_eq!(path, merkle.to_string());
                FlagSet::OPEN_FOR_READ.verify(flags);

                let stream = object.into_stream().unwrap().cast_stream();
                Blob { stream }
            }
            other => panic!("unexpected request: {:?}", other),
        }
    }

    /// Consume the next directory request, verifying it is intended to create the blob identified
    /// by `merkle`.  Returns a `Blob` representing the open blob file.
    ///
    /// # Panics
    ///
    /// Panics on error or assertion violation (unexpected requests or a mismatched open call)
    pub async fn expect_create_blob(&mut self, merkle: Hash) -> Blob {
        match self.stream.next().await {
            Some(Ok(fio::DirectoryRequest::Open {
                flags,
                mode: _,
                path,
                object,
                control_handle: _,
            })) => {
                FlagSet::OPEN_FOR_WRITE.verify(flags);
                assert_eq!(path, merkle.to_string());

                let stream = object.into_stream().unwrap().cast_stream();
                Blob { stream }
            }
            other => panic!("unexpected request: {:?}", other),
        }
    }

    async fn handle_rewind(&mut self) {
        match self.stream.next().await {
            Some(Ok(fio::DirectoryRequest::Rewind { responder })) => {
                responder.send(Status::OK.into_raw()).unwrap();
            }
            other => panic!("unexpected request: {:?}", other),
        }
    }

    /// Consume directory requests, verifying they are requests to read directory entries.  Respond
    /// with dirents constructed from the given entries.
    ///
    /// # Panics
    ///
    /// Panics on error or assertion violation (unexpected requests or not all entries are read)
    pub async fn expect_readdir(&mut self, entries: impl Iterator<Item = Hash>) {
        // fuchsia_fs::directory starts by resetting the directory channel's readdir position.
        self.handle_rewind().await;

        const NAME_LEN: usize = 64;
        #[repr(C, packed)]
        struct Dirent {
            ino: u64,
            size: u8,
            kind: u8,
            name: [u8; NAME_LEN],
        }

        impl Dirent {
            fn as_bytes(&self) -> &'_ [u8] {
                let start = self as *const Self as *const u8;
                // Safe because the FIDL wire format for directory entries is
                // defined to be the C packed struct representation used here.
                unsafe { std::slice::from_raw_parts(start, std::mem::size_of::<Self>()) }
            }
        }

        let mut entries_iter = entries.map(|hash| Dirent {
            ino: fio::INO_UNKNOWN,
            size: NAME_LEN as u8,
            kind: fio::DirentType::File.into_primitive(),
            name: hash.to_string().as_bytes().try_into().unwrap(),
        });

        loop {
            match self.stream.try_next().await.unwrap() {
                Some(fio::DirectoryRequest::ReadDirents { max_bytes, responder }) => {
                    let max_bytes = max_bytes as usize;
                    assert!(max_bytes >= std::mem::size_of::<Dirent>());

                    let mut buf = vec![];
                    while buf.len() + std::mem::size_of::<Dirent>() <= max_bytes {
                        match entries_iter.next() {
                            Some(need) => {
                                buf.extend(need.as_bytes());
                            }
                            None => break,
                        }
                    }

                    responder.send(Status::OK.into_raw(), &buf).unwrap();

                    // Finish after providing an empty chunk.
                    if buf.is_empty() {
                        break;
                    }
                }
                Some(other) => panic!("unexpected request: {:?}", other),
                None => panic!("unexpected stream termination"),
            }
        }
    }

    /// Consume N directory requests, verifying they are intended to determine whether the blobs
    /// specified `readable` and `missing` are readable or not, responding to the check based on
    /// which collection the hash is in.
    ///
    /// # Panics
    ///
    /// Panics on error or assertion violation (unexpected requests, request for unspecified blob)
    pub async fn expect_readable_missing_checks(&mut self, readable: &[Hash], missing: &[Hash]) {
        let mut readable = readable.iter().copied().collect::<HashSet<_>>();
        let mut missing = missing.iter().copied().collect::<HashSet<_>>();

        while !(readable.is_empty() && missing.is_empty()) {
            match self.stream.next().await {
                Some(Ok(fio::DirectoryRequest::Open {
                    flags,
                    mode: _,
                    path,
                    object,
                    control_handle: _,
                })) => {
                    FlagSet::OPEN_FOR_READ.verify(flags);
                    let path: Hash = path.parse().unwrap();

                    let stream = object.into_stream().unwrap().cast_stream();
                    let blob = Blob { stream };

                    if readable.remove(&path) {
                        blob.succeed_open_with_blob_readable().await;
                    } else if missing.remove(&path) {
                        blob.fail_open_with_not_found();
                    } else {
                        panic!("Unexpected blob existance check for {}", path);
                    }
                }
                other => panic!("unexpected request: {:?}", other),
            }
        }
    }

    /// Expects and handles a call to [`Client::filter_to_missing_blobs`], handling its internal
    /// heuristic when more than 20 blobs are specified.  Verifies the call intends to determine
    /// whether the blobs specified in `readable` and `missing` are readable or not, responding to
    /// the check based on which collection the hash is in.
    ///
    /// # Panics
    ///
    /// Panics on error or assertion violation (unexpected requests, request for unspecified blob)
    pub async fn expect_filter_to_missing_blobs_with_readable_missing_ids(
        &mut self,
        readable: &[Hash],
        missing: &[Hash],
    ) {
        // TODO(fxbug.dev/77717) re-evaluate filter_to_missing_blobs heuristic.
        if readable.len() + missing.len() > 20 {
            // heuristic path, handle the readdir, and trigger the fast path indicating
            // none of the missing blobs may be present by excluding them from the readdir.
            self.expect_readdir(readable.iter().copied()).await;
            self.expect_readable_missing_checks(readable, &[]).await;
        } else {
            self.expect_readable_missing_checks(readable, missing).await;
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
/// Blob does not send the OnOpen event or handle requests until instructed to do so.
pub struct Blob {
    stream: fio::FileRequestStream,
}

impl Blob {
    fn send_on_open_with_file_signals(&mut self, status: Status, signals: zx::Signals) {
        let event = fidl::Event::create().unwrap();
        event.signal_handle(zx::Signals::NONE, signals).unwrap();

        let mut info =
            fio::NodeInfoDeprecated::File(fio::FileObject { event: Some(event), stream: None });
        let () =
            self.stream.control_handle().send_on_open_(status.into_raw(), Some(&mut info)).unwrap();
    }

    fn send_on_open(&mut self, status: Status) {
        self.send_on_open_with_file_signals(status, zx::Signals::NONE);
    }

    fn send_on_open_with_readable(&mut self, status: Status) {
        // Send USER_0 signal to indicate that the blob is available.
        self.send_on_open_with_file_signals(status, zx::Signals::USER_0);
    }

    fn fail_open_with_error(mut self, status: Status) {
        assert_ne!(status, Status::OK);
        self.send_on_open(status);
    }

    /// Fail the open request with an error indicating the blob already exists.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn fail_open_with_already_exists(self) {
        self.fail_open_with_error(Status::ACCESS_DENIED);
    }

    /// Fail the open request with an error indicating the blob does not exist.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn fail_open_with_not_found(self) {
        self.fail_open_with_error(Status::NOT_FOUND);
    }

    /// Fail the open request with a generic IO error.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn fail_open_with_io_error(self) {
        self.fail_open_with_error(Status::IO);
    }

    /// Succeeds the open request, but indicate the blob is not yet readable by not asserting the
    /// USER_0 signal on the file event handle, then asserts that the connection to the blob is
    /// closed.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn fail_open_with_not_readable(mut self) {
        self.send_on_open(Status::OK);
        self.expect_done().await;
    }

    /// Succeeds the open request, indicating that the blob is readable, then asserts that the
    /// connection to the blob is closed.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn succeed_open_with_blob_readable(mut self) {
        self.send_on_open_with_readable(Status::OK);
        self.expect_done().await;
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
            Some(Ok(fio::FileRequest::Close { responder })) => {
                let _ = responder.send(&mut Ok(()));
                self.expect_done().await;
            }
            Some(other) => panic!("unexpected request: {:?}", other),
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

    async fn handle_read(&mut self, data: &[u8]) -> usize {
        match self.stream.next().await {
            Some(Ok(fio::FileRequest::Read { count, responder })) => {
                let count = min(count.try_into().unwrap(), data.len());
                responder.send(&mut Ok(data[..count].to_vec())).unwrap();
                count
            }
            other => panic!("unexpected request: {:?}", other),
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
            Some(Ok(fio::FileRequest::Close { responder })) => {
                let _ = responder.send(&mut Ok(()));
            }
            Some(other) => panic!("unexpected request: {:?}", other),
        }
    }

    /// Succeeds the open request. Then handles get_attr, read, read_at, and possibly a final close
    /// requests with the given blob data.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn serve_contents(mut self, data: &[u8]) {
        self.send_on_open_with_readable(Status::OK);

        let mut pos: usize = 0;

        loop {
            match self.stream.next().await {
                Some(Ok(fio::FileRequest::Read { count, responder })) => {
                    let avail = data.len() - pos;
                    let count = min(count.try_into().unwrap(), avail);
                    responder.send(&mut Ok(data[pos..pos + count].to_vec())).unwrap();
                    pos += count;
                }
                Some(Ok(fio::FileRequest::ReadAt { count, offset, responder })) => {
                    let pos: usize = offset.try_into().unwrap();
                    let avail = data.len() - pos;
                    let count = min(count.try_into().unwrap(), avail);
                    responder.send(&mut Ok(data[pos..pos + count].to_vec())).unwrap();
                }
                Some(Ok(fio::FileRequest::GetAttr { responder })) => {
                    let mut attr = fio::NodeAttributes::new_empty();
                    attr.content_size = data.len().try_into().unwrap();
                    responder.send(Status::OK.into_raw(), &mut attr).unwrap();
                }
                Some(Ok(fio::FileRequest::Close { responder })) => {
                    let _ = responder.send(&mut Ok(()));
                    return;
                }
                None => {
                    return;
                }
                other => panic!("unexpected request: {:?}", other),
            }
        }
    }

    async fn handle_truncate(&mut self, status: Status) -> u64 {
        match self.stream.next().await {
            Some(Ok(fio::FileRequest::Resize { length, responder })) => {
                responder
                    .send(&mut if status == Status::OK { Ok(()) } else { Err(status.into_raw()) })
                    .unwrap();

                length
            }
            other => panic!("unexpected request: {:?}", other),
        }
    }

    async fn expect_truncate(&mut self) -> u64 {
        self.handle_truncate(Status::OK).await
    }

    async fn handle_write(&mut self, status: Status) -> Vec<u8> {
        match self.stream.next().await {
            Some(Ok(fio::FileRequest::Write { data, responder })) => {
                responder
                    .send(&mut if status == Status::OK {
                        Ok(data.len() as u64)
                    } else {
                        Err(status.into_raw())
                    })
                    .unwrap();

                data
            }
            other => panic!("unexpected request: {:?}", other),
        }
    }

    async fn fail_write_with_status(mut self, status: Status) {
        self.send_on_open(Status::OK);

        let length = self.expect_truncate().await;
        // divide rounding up
        let expected_write_calls = (length + (fio::MAX_BUF - 1)) / fio::MAX_BUF;
        for _ in 0..(expected_write_calls - 1) {
            self.handle_write(Status::OK).await;
        }
        self.handle_write(status).await;
    }

    /// Succeeds the open request, consumes the truncate request, the initial write calls, then
    /// fails the final write indicating the written data was corrupt.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub async fn fail_write_with_corrupt(self) {
        self.fail_write_with_status(Status::IO_DATA_INTEGRITY).await
    }

    /// Succeeds the open request, then returns a future that, when awaited, verifies the blob is
    /// truncated, written, and closed with the given `expected` payload.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn expect_payload(mut self, expected: &[u8]) -> impl Future<Output = ()> + '_ {
        self.send_on_open(Status::OK);

        async move {
            assert_eq!(self.expect_truncate().await, expected.len() as u64);

            let mut rest = expected;
            while !rest.is_empty() {
                let expected_chunk = if rest.len() > fio::MAX_BUF as usize {
                    &rest[..fio::MAX_BUF as usize]
                } else {
                    rest
                };
                assert_eq!(self.handle_write(Status::OK).await, expected_chunk);
                rest = &rest[expected_chunk.len()..];
            }

            match self.stream.next().await {
                Some(Ok(fio::FileRequest::Close { responder })) => {
                    responder.send(&mut Ok(())).unwrap();
                }
                other => panic!("unexpected request: {:?}", other),
            }

            self.expect_done().await;
        }
    }
}

#[derive(Copy, Clone, Debug)]
struct FlagSet {
    required: fio::OpenFlags,
    anti_required: fio::OpenFlags,
}

impl FlagSet {
    const OPEN_FOR_READ: FlagSet = FlagSet::new()
        .require_present(fio::OpenFlags::RIGHT_READABLE)
        .require_absent(fio::OpenFlags::CREATE)
        .require_absent(fio::OpenFlags::RIGHT_WRITABLE);

    const OPEN_FOR_WRITE: FlagSet = FlagSet::new()
        .require_present(fio::OpenFlags::CREATE)
        .require_present(fio::OpenFlags::RIGHT_WRITABLE);

    const fn new() -> Self {
        Self { required: fio::OpenFlags::empty(), anti_required: fio::OpenFlags::empty() }
    }
    const fn require_present(self, flags: fio::OpenFlags) -> Self {
        let Self { required, anti_required } = self;
        let required = required.union(flags);
        Self { required, anti_required }
    }
    const fn require_absent(self, flags: fio::OpenFlags) -> Self {
        let Self { required, anti_required } = self;
        let anti_required = anti_required.union(flags);
        Self { required, anti_required }
    }
    fn verify(self, flags: fio::OpenFlags) {
        assert_eq!(flags & self.required, self.required);
        assert_eq!(flags & self.anti_required, fio::OpenFlags::empty());
    }
}
