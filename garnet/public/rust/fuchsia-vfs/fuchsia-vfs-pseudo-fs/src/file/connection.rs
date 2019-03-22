// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of an individual connection to a file.

use {
    fidl::encoding::OutOfLine,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        FileMarker, FileObject, FileRequestStream, NodeInfo, NodeMarker, OPEN_FLAG_DESCRIBE,
    },
    fuchsia_zircon::Status,
    futures::{
        stream::{Stream, StreamExt, StreamFuture},
        task::Waker,
        Poll,
    },
    std::pin::Pin,
};

/// FileConnection represents the buffered connection of a single client to a pseudo file. It
/// implements Stream, which proxies file requests from the contained FileRequestStream.
pub struct FileConnection {
    requests: FileRequestStream,
    /// Either the "flags" value passed into [`DirectoryEntry::open()`], or the "flags" value
    /// passed into FileRequest::Clone().
    pub flags: u32,
    /// Seek position.  Next byte to be read or written within the buffer.  This might be beyond
    /// the current size of buffer, matching POSIX:
    ///
    ///     http://pubs.opengroup.org/onlinepubs/9699919799/functions/lseek.html
    ///
    /// It will cause the buffern to be extended with zeroes (if necessary) when write() is called.
    // While the content in the buffer vector uses usize for the size, it is easier to use u64 to
    // match the FIDL bindings API.  Pseudo files are not expected to cross the 2^64 bytes size
    // limit.  And all the code is much simpler when we just assume that usize is the same as u64.
    // Should we need to port to a 128 bit platform, there are static assertions in the code that
    // would fail.
    pub seek: u64,
    /// Per connection buffer.  See module documentation for details.
    pub buffer: Vec<u8>,
    /// Starts as false, and causes the [`on_write()`] to be called when the connection is closed
    /// if set to true during the lifetime of the connection.
    pub was_written: bool,
}

impl FileConnection {
    /// Initialized a file connection, checking flags and sending an `OnOpen` event if necessary.
    /// Returns a [`FileConnection`] object as a [`StreamFuture`], or in the case of an error, sends
    /// an appropriate `OnOpen` event (if requested) and returns `Err`.
    pub fn connect(
        flags: u32,
        server_end: ServerEnd<NodeMarker>,
        buffer: Vec<u8>,
        was_written: bool,
    ) -> Result<StreamFuture<FileConnection>, fidl::Error> {
        let (requests, control_handle) = ServerEnd::<FileMarker>::new(server_end.into_channel())
            .into_stream_and_control_handle()?;

        let conn = (FileConnection { requests, flags, seek: 0, buffer, was_written }).into_future();

        if flags & OPEN_FLAG_DESCRIBE != 0 {
            let mut info = NodeInfo::File(FileObject { event: None });
            control_handle.send_on_open_(Status::OK.into_raw(), Some(OutOfLine(&mut info)))?;
        }

        Ok(conn)
    }
}

/// Allow [`FileConnection`] to be wrapped in a [`StreamFuture`], to be further contained inside
/// [`FuturesUnordered`].
impl Stream for FileConnection {
    // We are just proxying the FileRequestStream requests.
    type Item = <FileRequestStream as Stream>::Item;

    fn poll_next(mut self: Pin<&mut Self>, lw: &Waker) -> Poll<Option<Self::Item>> {
        self.requests.poll_next_unpin(lw)
    }
}
