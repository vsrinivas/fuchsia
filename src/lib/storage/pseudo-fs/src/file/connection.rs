// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of an individual connection to a file.

use crate::{common::send_on_open_with_error, file::common::new_connection_validate_flags};

use {
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        FileMarker, FileObject, FileRequest, FileRequestStream, NodeAttributes, NodeInfo,
        NodeMarker, SeekOrigin, INO_UNKNOWN, MODE_TYPE_FILE, OPEN_FLAG_DESCRIBE,
        OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_mem,
    fuchsia_zircon::{
        sys::{ZX_ERR_NOT_SUPPORTED, ZX_OK},
        Status, Vmo,
    },
    futures::{
        stream::{Stream, StreamExt, StreamFuture},
        FutureExt,
    },
    pin_utils::{unsafe_pinned, unsafe_unpinned},
    static_assertions::assert_eq_size,
    std::{
        future::Future,
        io::Write,
        mem,
        pin::Pin,
        task::{Context, Poll},
    },
};

/// Return type for [`handle_request`] functions.
pub enum ConnectionState {
    /// Connection is still alive.
    Alive,
    /// Connection have received Node::Close message and the [`handle_close`] method has been
    /// already called for this connection.
    Closed,
    /// Connection has been dropped by the peer or an error has occured.  [`handle_close`] still
    /// need to be called (though it would not be able to report the status to the peer).
    Dropped,
}

/// FileConnection represents the buffered connection of a single client to a pseudo file. It
/// implements Stream, which proxies file requests from the contained FileRequestStream.
pub struct FileConnection {
    requests: FileRequestStream,
    /// Either the "flags" value passed into [`DirectoryEntry::open()`], or the "flags" value
    /// passed into FileRequest::Clone().
    pub flags: u32,
    /// MODE_PROTECTION_MASK attributes returned by this file through io.fild:Node::GetAttr.  They
    /// have no meaning for the file operation itself, but may have consequences to the POSIX
    /// emulation layer - for example, it makes sense to remove the read flags from a read-only
    /// file.  This field should only have set bits in the MODE_PROTECTION_MASK part.
    protection_attributes: u32,
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
    /// Maximum size the buffer that holds the value written into this file can grow to.  When the
    /// buffer is populated by a the [`on_read`] handler, this restriction is not enforced.  The
    /// maximum size of the buffer passed into [`on_write`] is the maximum of the size of the
    /// buffer that [`on_read`] have returnd and this value.
    capacity: u64,
    /// Per connection buffer.  See module documentation for details.
    buffer: Vec<u8>,
    /// Starts as false, and causes the [`on_write()`] to be called when the connection is closed
    /// if set to true during the lifetime of the connection.
    was_written: bool,
}

pub enum InitialConnectionState<BufferFut>
where
    BufferFut: Future<Output = Result<Vec<u8>, Status>> + Send,
{
    Failed,
    Pending(FileConnectionFuture<BufferFut>),
    Ready(StreamFuture<FileConnection>),
}

/// BufferResult is the result of the InitBuffer for connect_async. It is essentially an option, but
/// with a more descriptive name and branches.
pub enum BufferResult<R> {
    Future(R),
    Empty,
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
        flags: u32,
        mode: u32,
        protection_attributes: u32,
        server_end: ServerEnd<NodeMarker>,
        readable: bool,
        writable: bool,
        capacity: u64,
        init_buffer: InitBuffer,
    ) -> Option<StreamFuture<Self>>
    where
        InitBuffer: FnOnce(u32) -> Result<(Vec<u8>, bool), Status>,
    {
        let flags = match new_connection_validate_flags(flags, mode, readable, writable) {
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

        Self::create_connection(
            flags,
            protection_attributes,
            server_end,
            capacity,
            buffer,
            was_written,
        )
    }

    pub fn connect_async<InitBuffer, OnReadRes>(
        flags: u32,
        mode: u32,
        protection_attributes: u32,
        server_end: ServerEnd<NodeMarker>,
        readable: bool,
        writable: bool,
        capacity: u64,
        init_buffer: InitBuffer,
    ) -> InitialConnectionState<OnReadRes>
    where
        InitBuffer: FnOnce(u32) -> (BufferResult<OnReadRes>, bool),
        OnReadRes: Future<Output = Result<Vec<u8>, Status>> + Send,
    {
        let flags = match new_connection_validate_flags(flags, mode, readable, writable) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return InitialConnectionState::Failed;
            }
        };

        let (maybe_buffer_future, was_written) = init_buffer(flags);

        if let BufferResult::Future(buffer_future) = maybe_buffer_future {
            // if we are making a future, init buffer will be returning false for was_written. see
            // FileConnectionFuture for details.
            debug_assert!(!was_written, "init_buffer returned was_written == true");
            let fut = FileConnectionFuture::new(
                flags,
                protection_attributes,
                server_end,
                capacity,
                buffer_future,
            );
            InitialConnectionState::Pending(fut)
        } else {
            match Self::create_connection(
                flags,
                protection_attributes,
                server_end,
                capacity,
                vec![],
                was_written,
            ) {
                None => InitialConnectionState::Failed,
                Some(conn) => InitialConnectionState::Ready(conn),
            }
        }
    }

    // pub(self) so that FileConnectionFuture can use it too.
    pub(self) fn create_connection(
        flags: u32,
        protection_attributes: u32,
        server_end: ServerEnd<NodeMarker>,
        capacity: u64,
        buffer: Vec<u8>,
        was_written: bool,
    ) -> Option<StreamFuture<Self>> {
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
            let mut info = NodeInfo::File(FileObject { event: None, stream: None });
            match control_handle.send_on_open_(Status::OK.into_raw(), Some(&mut info)) {
                Ok(()) => (),
                Err(_) => return None,
            }
        }

        let conn = (FileConnection {
            requests,
            flags,
            protection_attributes,
            seek: 0,
            capacity,
            buffer,
            was_written,
        })
        .into_future();
        Some(conn)
    }

    /// Handle a [`FileRequest`]. This function essentially provides a default implementation for
    /// basic file operations that operate on the connection-specific buffer. It is expected that
    /// implementations of pseudo files implement their own wrapping handle_request function that
    /// implements [`FileRequest::Clone`] and [`FileRequest::Close`], as these can't be implemented
    /// by the connection.
    // TODO(fxbug.dev/37419): Remove default handling after methods landed.
    #[allow(unreachable_patterns)]
    pub fn handle_request(&mut self, req: FileRequest) -> Result<(), Error> {
        match req {
            // these two should be handled by the file-specific handle_request functions
            FileRequest::Clone { flags: _, object: _, control_handle: _ } => {
                panic!("Bug: Clone can't be handled by the connection object");
            }
            FileRequest::Close { responder: _ } => {
                panic!("Bug: Close can't be handled by the connection object");
            }
            FileRequest::Describe { responder } => {
                let mut info = NodeInfo::File(FileObject { event: None, stream: None });
                responder.send(&mut info)?;
            }
            FileRequest::Sync { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            FileRequest::GetAttr { responder } => {
                let mut attrs = NodeAttributes {
                    mode: MODE_TYPE_FILE | self.protection_attributes,
                    id: INO_UNKNOWN,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                };
                responder.send(ZX_OK, &mut attrs)?;
            }
            FileRequest::SetAttr { flags: _, attributes: _, responder } => {
                // According to zircon/system/fidl/fuchsia-io/io.fidl the only flag that might be
                // modified through this call is OPEN_FLAG_APPEND, and it is not supported by the
                // PseudoFile.
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            FileRequest::Read { count, responder } => {
                self.handle_read(count, |status, content| {
                    responder.send(status.into_raw(), content)
                })?;
            }
            FileRequest::ReadAt { offset, count, responder } => {
                self.handle_read_at(offset, count, |status, content| {
                    responder.send(status.into_raw(), content)
                })?;
            }
            FileRequest::Write { data, responder } => {
                self.handle_write(data, |status, actual| {
                    responder.send(status.into_raw(), actual)
                })?;
            }
            FileRequest::WriteAt { offset, data, responder } => {
                self.handle_write_at(offset, data, |status, actual| {
                    // Seems like our API is not really designed for 128 bit machines. If data
                    // contains more than 16EB, we may not be returning the correct number here.
                    responder.send(status.into_raw(), actual as u64)
                })?;
            }
            FileRequest::Seek { offset, start, responder } => {
                self.handle_seek(offset, start, |status, offset| {
                    responder.send(status.into_raw(), offset)
                })?;
            }
            FileRequest::Truncate { length, responder } => {
                self.handle_truncate(length, |status| responder.send(status.into_raw()))?;
            }
            FileRequest::GetFlags { responder } => {
                responder.send(ZX_OK, self.flags)?;
            }
            FileRequest::SetFlags { flags: _, responder } => {
                // TODO: Support OPEN_FLAG_APPEND?  It is the only flag that is allowed to be set
                // via this call according to the io.fidl.  It would be nice to have that
                // explicitly encoded in the API instead, I guess.
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            FileRequest::GetBuffer { flags, responder } => {
                self.handle_get_buffer(flags, |status, mut buffer| {
                    responder.send(status.into_raw(), buffer.as_mut())
                })?;
            }
            _ => {}
        }
        Ok(())
    }

    /// Read `count` bytes at the current seek value from the buffer associated with the connection.
    /// The content is sent as an iterator to the provided responder. It increases the current seek
    /// position by the actual number of bytes written. If an error occurs, an error code is sent to
    /// the responder with an empty iterator, and this function returns `Ok(())`. If the responder
    /// returns an error when used (including in the successful case), this function returns that
    /// error directly.
    fn handle_read<R>(&mut self, count: u64, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, &[u8]) -> Result<(), fidl::Error>,
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
    fn handle_read_at<R>(
        &mut self,
        offset: u64,
        mut count: u64,
        responder: R,
    ) -> Result<u64, fidl::Error>
    where
        R: FnOnce(Status, &[u8]) -> Result<(), fidl::Error>,
    {
        if self.flags & OPEN_RIGHT_READABLE == 0 {
            responder(Status::ACCESS_DENIED, &[])?;
            return Ok(0);
        }

        assert_eq_size!(usize, u64);

        let len = self.buffer.len() as u64;

        if offset >= len {
            // This should return Status::OUT_OF_RANGE but POSIX wants an OK.  See ZX-3633.
            responder(Status::OK, &[])?;
            return Ok(0);
        }

        count = core::cmp::min(count, len - offset);

        let from = offset as usize;
        let to = (offset + count) as usize;
        responder(Status::OK, &self.buffer[from..to])?;
        Ok(count)
    }

    /// Copy the contents of the buffer associated with the connection into a VMO and return it. If
    /// an error occurs, an error code is sent to the responder with no VMO, and this function
    /// returns `Ok(())`. If the responder returns an error when used (including in the successful
    /// case), this function returns that error directly.
    fn handle_get_buffer<R>(&mut self, flags: u32, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, Option<fidl_fuchsia_mem::Buffer>) -> Result<(), fidl::Error>,
    {
        match (
            self.flags & OPEN_RIGHT_READABLE != 0, // This file is readable
            self.flags & OPEN_RIGHT_WRITABLE != 0, // This file is writable
            flags & OPEN_RIGHT_READABLE != 0,      // The requested buffer is readable
            flags & OPEN_RIGHT_WRITABLE != 0,      // The requested buffer is writable
        ) {
            // Read-only buffers can be retrieved for read-only files
            (true, false, true, false) => (),
            // Accessing writable buffers is currently unsupported
            (_, true, _, _) => return responder(Status::NOT_SUPPORTED, None),
            // All other situations are not permitted
            _ => return responder(Status::ACCESS_DENIED, None),
        }

        assert_eq_size!(usize, u64);

        let size = self.buffer.len() as u64;

        let vmo = Vmo::create(size).expect("VMO creation failed");
        vmo.write(&self.buffer[..], 0).expect("VMO writing failed");

        responder(Status::OK, Some(fidl_fuchsia_mem::Buffer { size, vmo }))
    }

    /// Write `content` at the current seek position in the buffer associated with the connection.
    /// The corresponding pseudo file should have a size `capacity`. On a successful write, the
    /// number of bytes written is sent to `responder` and also returned from this function. The seek
    /// position is increased by the number of bytes written. On an error, the error code is sent to
    /// `responder`, and this function returns `Ok(())`. If the responder returns an error, this
    /// funtion forwards that error back to the caller.
    // Strictly speaking, we do not need to use a callback here, but we do need it in the on_read()
    // case above, so, for consistency, on_write() has the same interface.
    fn handle_write<R>(&mut self, content: Vec<u8>, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, u64) -> Result<(), fidl::Error>,
    {
        let actual = self.handle_write_at(self.seek, content, responder)?;
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
    fn handle_write_at<R>(
        &mut self,
        offset: u64,
        content: Vec<u8>,
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
        let effective_capacity = core::cmp::max(self.buffer.len() as u64, self.capacity);

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
    fn handle_seek<R>(
        &mut self,
        offset: i64,
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

        let effective_capacity = core::cmp::max(self.buffer.len() as i128, self.capacity as i128);
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
    fn handle_truncate<R>(&mut self, length: u64, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        if self.flags & OPEN_RIGHT_WRITABLE == 0 {
            return responder(Status::ACCESS_DENIED);
        }

        let effective_capacity = core::cmp::max(self.buffer.len() as u64, self.capacity);

        if length > effective_capacity {
            return responder(Status::OUT_OF_RANGE);
        }

        assert_eq_size!(usize, u64);

        self.buffer.resize(length as usize, 0);

        // We are not supposed to touch the seek position during truncation, but the
        // effective_capacity may be smaller now - in which case we do need to move the seek
        // position.
        let new_effective_capacity = core::cmp::max(length, self.capacity);
        self.seek = core::cmp::min(self.seek, new_effective_capacity);

        responder(Status::OK)
    }

    /// Prepare for closing the connection. Takes a function to pass the connection buffer to.
    /// Returns the result of the function if it was called, or `when_unmodified` when the buffer
    /// wasn't modified.
    pub fn handle_close<Write, R>(&mut self, write: Write, when_unmodified: R) -> R
    where
        Write: FnOnce(Vec<u8>) -> R,
    {
        if !self.was_written {
            return when_unmodified;
        }

        write(mem::replace(&mut self.buffer, vec![]))
    }
}

/// Allow [`FileConnection`] to be wrapped in a [`StreamFuture`], to be further contained inside
/// [`FuturesUnordered`].
impl Stream for FileConnection {
    // We are just proxying the FileRequestStream requests.
    type Item = <FileRequestStream as Stream>::Item;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        self.requests.poll_next_unpin(cx)
    }
}

/// A wrapping future for the result of an on_read function call. It transforms the on_read return
/// value into a FileConnection, and thus stores all the necessary additional information for that
/// transformation.
///
/// When creating a file connection, this future hard codes was_written to be false. At this point, a
/// bunch of situations are handled for us. The fact that this future exists means -
///  - on_read exists
///  - the file permissions are marked as readable
///  - we aren't truncating the file
/// The only time during connection creation that we expect was_written to be set to true is when we
/// are truncating, so hard-coding was_written to false is fine.
pub struct FileConnectionFuture<BufferFut>
where
    BufferFut: Future<Output = Result<Vec<u8>, Status>> + Send,
{
    flags: u32,
    protection_attributes: u32,
    server_end: Option<ServerEnd<NodeMarker>>,
    capacity: u64,
    res: BufferFut,
}

impl<BufferFut> FileConnectionFuture<BufferFut>
where
    BufferFut: Future<Output = Result<Vec<u8>, Status>> + Send,
{
    // unsafe: `server_end` is not referenced by any other field in the `OnWriteFuture`, so it is
    // safe to get a mutable reference from a pinned one.
    unsafe_unpinned!(server_end: Option<ServerEnd<NodeMarker>>);
    // unsafe: `FileConnectionFuture` does not implement `Drop`, or `Unpin`.  And it is not
    // `#[repr(packed)]`.
    unsafe_pinned!(res: BufferFut);

    pub fn new(
        flags: u32,
        protection_attributes: u32,
        server_end: ServerEnd<NodeMarker>,
        capacity: u64,
        buffer_future: BufferFut,
    ) -> Self {
        FileConnectionFuture {
            flags,
            protection_attributes,
            server_end: Some(server_end),
            capacity,
            res: buffer_future,
        }
    }
}

impl<BufferFut> Future for FileConnectionFuture<BufferFut>
where
    BufferFut: Future<Output = Result<Vec<u8>, Status>> + Send,
{
    type Output = Option<StreamFuture<FileConnection>>;

    fn poll(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Self::Output> {
        match self.as_mut().res().poll_unpin(cx) {
            Poll::Ready(Ok(buf)) => {
                // unwrap is "safe" in that it only happens when we are returning Poll::Ready, which
                // means any subsequent calls to poll can do nasty things (like panic!)
                let server_end = self.as_mut().server_end().take().unwrap();
                let conn = FileConnection::create_connection(
                    self.flags,
                    self.protection_attributes,
                    server_end,
                    self.capacity,
                    buf,
                    false,
                );
                Poll::Ready(conn)
            }
            // if on_read returns an error, we want to attempt to signal that something went wrong.
            Poll::Ready(Err(status)) => {
                // same reasoning here as above for the safety of unwrapping this value.
                let server_end = self.as_mut().server_end().take().unwrap();
                send_on_open_with_error(self.flags, server_end, status);
                Poll::Ready(None)
            }
            Poll::Pending => Poll::Pending,
        }
    }
}
