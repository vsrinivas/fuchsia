// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of an individual connection to a file.

use {
    crate::common::send_on_open_with_error,
    crate::file::common::new_connection_validate_flags,
    fidl::{encoding::OutOfLine, endpoints::ServerEnd},
    fidl_fuchsia_io::{
        FileMarker, FileObject, FileRequestStream, NodeInfo, NodeMarker, SeekOrigin,
        OPEN_FLAG_DESCRIBE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon::Status,
    futures::{
        stream::{Stream, StreamExt, StreamFuture},
        task::Waker,
        Poll,
    },
    std::{io::Write, iter, pin::Pin},
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
    seek: u64,
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

    /// Read `count` bytes at the current seek value from the buffer associated with the connection.
    /// The content is sent as an iterator to the provided responder. It increases the current seek
    /// position by the actual number of bytes written. If an error occurs, an error code is sent to
    /// the responder with an empty iterator, and this function returns `Ok(())`. If the responder
    /// returns an error when used (including in the successful case), this function returns that
    /// error directly.
    pub fn handle_read<R>(&mut self, count: u64, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, &mut ExactSizeIterator<Item = u8>) -> Result<(), fidl::Error>,
    {
        let actual = self.handle_read_at(self.seek, count, responder)?;
        self.seek += actual;
        Ok(())
    }

    /// Read `count` bytes at `offset` from the buffer associated with the connection. The content is
    /// sent as an iterator to the provided responder. If an error occurs, an error code is sent to
    /// the responder with an empty iterator, and this function returns `Ok(0)`. If the responder
    /// returns an error when used (including in the successful case), this function returns that
    /// error directly.
    pub fn handle_read_at<R>(
        &mut self,
        offset: u64,
        mut count: u64,
        responder: R,
    ) -> Result<u64, fidl::Error>
    where
        R: FnOnce(Status, &mut ExactSizeIterator<Item = u8>) -> Result<(), fidl::Error>,
    {
        if self.flags & OPEN_RIGHT_READABLE == 0 {
            responder(Status::ACCESS_DENIED, &mut iter::empty())?;
            return Ok(0);
        }

        assert_eq_size!(usize, u64);

        let len = self.buffer.len() as u64;

        if offset >= len {
            // This should return Status::OUT_OF_RANGE but POSIX wants an OK.  See ZX-3633.
            responder(Status::OK, &mut iter::empty())?;
            return Ok(0);
        }

        count = core::cmp::min(count, len - offset);

        let from = offset as usize;
        let to = (offset + count) as usize;
        let mut content = self.buffer[from..to].iter().cloned();
        responder(Status::OK, &mut content)?;
        Ok(count)
    }

    /// Write `content` at the current seek position in the buffer associated with the connection.
    /// The corresponding pseudo file should have a size `capacity`. On a successful write, the
    /// number of bytes written is sent to `responder` and also returned from this function. The seek
    /// position is increased by the number of bytes written. On an error, the error code is sent to
    /// `responder`, and this function returns `Ok(())`. If the responder returns an error, this
    /// funtion forwards that error back to the caller.
    // Strictly speaking, we do not need to use a callback here, but we do need it in the on_read()
    // case above, so, for consistency, on_write() has the same interface.
    pub fn handle_write<R>(
        &mut self,
        content: Vec<u8>,
        capacity: u64,
        responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, u64) -> Result<(), fidl::Error>,
    {
        let actual = self.handle_write_at(self.seek, content, capacity, responder)?;
        self.seek += actual;
        Ok(())
    }

    /// Write `content` at `offset` in the buffer associated with the connection. The corresponding
    /// pseudo file should have a size `capacity`. On a successful write, the number of bytes written
    /// is sent to `responder` and also returned from this function. On an error, the error code is
    /// sent to `responder`, and this function returns `Ok(0)`. If the responder returns an error,
    /// this funtion forwards that error back to the caller.
    // Strictly speaking, we do not need to use a callback here, but we do need it in the on_read()
    // case above, so, for consistency, on_write() has the same interface.
    pub fn handle_write_at<R>(
        &mut self,
        offset: u64,
        content: Vec<u8>,
        capacity: u64,
        responder: R,
    ) -> Result<u64, fidl::Error>
    where
        R: FnOnce(Status, u64) -> Result<(), fidl::Error>,
    {
        if self.flags & OPEN_RIGHT_WRITABLE == 0 {
            responder(Status::ACCESS_DENIED, 0)?;
            return Ok(0);
        }

        assert_eq_size!(usize, u64);
        let effective_capacity = core::cmp::max(self.buffer.len() as u64, capacity);

        if offset >= effective_capacity {
            responder(Status::OUT_OF_RANGE, 0)?;
            return Ok(0);
        }

        assert_eq_size!(usize, u64);

        let actual = core::cmp::min(effective_capacity - offset, content.len() as u64);

        let buffer = &mut self.buffer;

        if buffer.len() as u64 <= offset + actual {
            buffer.resize((offset + actual) as usize, 0);
        }

        let from = offset as usize;
        let to = (offset + actual) as usize;
        let mut target = &mut buffer[from..to];
        let source = &content[0..actual as usize];
        target.write_all(source).unwrap();

        self.was_written = true;

        responder(Status::OK, actual)?;
        Ok(actual)
    }

    /// Move seek position to byte `offset` in the buffer associated with this connection. The
    /// corresponding pseudo file should have a size of `capacity`. On a successful write, the new
    /// seek position is sent to `responder` and this function returns `Ok(())`. On an error, the
    /// error code is sent to `responder`, and this function returns `Ok(())`. If the responder
    /// returns an error, this funtion forwards that error back to the caller.
    pub fn handle_seek<R>(
        &mut self,
        offset: i64,
        capacity: u64,
        start: SeekOrigin,
        responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, u64) -> Result<(), fidl::Error>,
    {
        let new_seek = match start {
            SeekOrigin::Start => offset as i128,

            SeekOrigin::Current => {
                assert_eq_size!(usize, i64);
                self.seek as i128 + offset as i128
            }

            SeekOrigin::End => {
                assert_eq_size!(usize, i64, u64);
                self.buffer.len() as i128 + offset as i128
            }
        };

        let effective_capacity = core::cmp::max(self.buffer.len() as i128, capacity as i128);
        if new_seek < 0 || new_seek >= effective_capacity {
            responder(Status::OUT_OF_RANGE, self.seek)?;
            return Ok(());
        }
        let new_seek = new_seek as u64;

        self.seek = new_seek;
        responder(Status::OK, new_seek)
    }

    /// Truncate to `length` the buffer associated with the connection. The corresponding pseudo file
    /// should have a size `capacity`. If after the truncation the seek position would be beyond the
    /// new end of the buffer, it is set to the end of the buffer. On a successful truncate,
    /// [`Status::OK`] is sent to `responder`. On an error, the error code is sent to `responder`,
    /// and this function returns `Ok(())`. If the responder returns an error, this funtion forwards
    /// that error back to the caller.
    pub fn handle_truncate<R>(
        &mut self,
        length: u64,
        capacity: u64,
        responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        if self.flags & OPEN_RIGHT_WRITABLE == 0 {
            return responder(Status::ACCESS_DENIED);
        }

        let effective_capacity = core::cmp::max(self.buffer.len() as u64, capacity);

        if length > effective_capacity {
            return responder(Status::OUT_OF_RANGE);
        }

        assert_eq_size!(usize, u64);

        self.buffer.resize(length as usize, 0);

        // We are not supposed to touch the seek position during truncation, but the
        // effective_capacity may be smaller now - in which case we do need to move the seek
        // position.
        let new_effective_capacity = core::cmp::max(length, capacity);
        self.seek = core::cmp::min(self.seek, new_effective_capacity);

        responder(Status::OK)
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
