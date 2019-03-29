// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of an individual connection to a file.

use {
    crate::common::send_on_open_with_error,
    crate::file::common::new_connection_validate_flags,
    fidl::{encoding::OutOfLine, endpoints::ServerEnd},
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
    /// an appropriate `OnOpen` event (if requested) and returns `None`.
    ///
    /// Per connection buffer is initialized using the `init_buffer` closure, which is passed
    /// `flags` value that might be adjusted for normalization.  Closue should return new buffer
    /// content and a "dirty" flag, or an error to send in `OnOpen`.
    pub fn connect<InitBuffer>(
        parent_flags: u32,
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
        readable: bool,
        writable: bool,
        init_buffer: InitBuffer,
    ) -> Option<StreamFuture<FileConnection>>
    where
        InitBuffer: FnOnce(u32) -> Result<(Vec<u8>, bool), Status>,
    {
        let flags =
            match new_connection_validate_flags(parent_flags, flags, mode, readable, writable) {
                Ok(updated) => updated,
                Err(status) => {
                    send_on_open_with_error(flags, server_end, status);
                    return None;
                }
            };

        let (buffer, was_written) = match init_buffer(flags) {
            Ok((buffer, was_written)) => (buffer, was_written),
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return None;
            }
        };

        // As we report all errors on `server_end`, if we failed to send an error in there, there
        // is nowhere to send it to.
        let (requests, control_handle) =
            match ServerEnd::<FileMarker>::new(server_end.into_channel())
                .into_stream_and_control_handle()
            {
                Ok((requests, control_handle)) => (requests, control_handle),
                Err(_) => return None,
            };

        if flags & OPEN_FLAG_DESCRIBE != 0 {
            let mut info = NodeInfo::File(FileObject { event: None });
            match control_handle.send_on_open_(Status::OK.into_raw(), Some(OutOfLine(&mut info))) {
                Ok(()) => (),
                Err(_) => return None,
            }
        }

        let conn = (FileConnection { requests, flags, seek: 0, buffer, was_written }).into_future();
        Some(conn)
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
