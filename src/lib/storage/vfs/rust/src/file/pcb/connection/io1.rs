// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of an individual connection to a file.

use crate::{
    common::{inherit_rights_for_clone, send_on_open_with_error, GET_FLAGS_VISIBLE},
    execution_scope::ExecutionScope,
    file::{
        common::{
            new_connection_validate_flags, POSIX_READ_ONLY_PROTECTION_ATTRIBUTES,
            POSIX_READ_WRITE_PROTECTION_ATTRIBUTES, POSIX_WRITE_ONLY_PROTECTION_ATTRIBUTES,
        },
        pcb::connection::{AsyncInitBuffer, AsyncUpdate, FileWithPerConnectionBuffer},
    },
};

use {
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        FileMarker, FileObject, FileRequest, FileRequestStream, NodeAttributes, NodeInfo,
        NodeMarker, SeekOrigin, INO_UNKNOWN, MODE_TYPE_FILE, OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_TRUNCATE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_zircon::{
        sys::{ZX_ERR_NOT_SUPPORTED, ZX_OK},
        Status,
    },
    futures::stream::StreamExt,
    static_assertions::assert_eq_size,
    std::{io::Write, mem, sync::Arc},
};

/// Represents a FIDL connection to a file.
pub struct FileConnection {
    /// Execution scope this connection and any async operations and connections it creates will
    /// use.
    scope: ExecutionScope,

    /// File this connection is associated with.
    file: Arc<dyn FileWithPerConnectionBuffer>,

    /// Wraps a FIDL connection, providing messages coming from the client.
    requests: FileRequestStream,

    /// Either the "flags" value passed into [`DirectoryEntry::open()`], or the "flags" value
    /// received with [`FileRequest::Clone()`].
    flags: u32,

    /// Flag passed into `create_connection`, that is used to limit read operations on this
    /// connection.
    readable: bool,

    /// Flag passed into `create_connection`, that is used to limit write operations on this
    /// connection.
    writable: bool,

    /// Seek position. Next byte to be read or written within the buffer. This might be beyond the
    /// current size of buffer, matching POSIX:
    ///
    ///     http://pubs.opengroup.org/onlinepubs/9699919799/functions/lseek.html
    ///
    /// It will cause the buffer to be extended with zeroes (if necessary) when write() is called.
    // While the content in the buffer vector uses usize for the size, it is easier to use u64 to
    // match the FIDL bindings API. Pseudo files are not expected to cross the 2^64 bytes size
    // limit. And all the code is much simpler when we just assume that usize is the same as u64.
    // Should we need to port to a 128 bit platform, there are static assertions in the code that
    // would fail.
    seek: u64,

    /// Maximum size the buffer that holds the value written into this file can grow to. When the
    /// buffer is populated by the [`init_buffer`] handler, this restriction is not enforced. The
    /// maximum size of the buffer passed into [`update`] is the maximum of the size of the buffer
    /// that [`init_buffer`] have returned and this value.
    capacity: u64,

    /// Per connection buffer. See module documentation for details.
    buffer: Vec<u8>,

    /// Starts as false, and causes the [`update()`] to be called when the connection is closed if
    /// set to true during the lifetime of the connection.
    was_written: bool,
}

/// Return type for [`handle_request()`] functions.
enum ConnectionState {
    /// Connection is still alive.
    Alive,
    /// Connection have received Node::Close message and the [`handle_close`] method has been
    /// already called for this connection.
    Closed,
    /// Connection has been dropped by the peer or an error has occured.  [`handle_close`] still
    /// need to be called (though it would not be able to report the status to the peer).
    Dropped,
}

impl FileConnection {
    /// Initialized a file connection, which will be running in the context of the specified
    /// execution `scope`.  This function will also check the flags and will send the `OnOpen`
    /// event if necessary.
    ///
    /// Per connection buffer is initialized using the `init_buffer` closure, as part of the
    /// connection initialization.
    pub fn create_connection(
        scope: ExecutionScope,
        file: Arc<dyn FileWithPerConnectionBuffer>,
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
        readable: bool,
        writable: bool,
        capacity: u64,
    ) {
        let task = Self::create_connection_task(
            scope.clone(),
            file,
            flags,
            mode,
            server_end,
            readable,
            writable,
            capacity,
        );
        // If we failed to send the task to the executor, it is probably shut down or is in the
        // process of shutting down (this is the only error state currently).  So there is nothing
        // for us to do, but to ignore the open.  `server_end` will be closed when the object will
        // be dropped - there seems to be no error to report there.
        let _ = scope.spawn(Box::pin(task));
    }

    async fn create_connection_task(
        scope: ExecutionScope,
        file: Arc<dyn FileWithPerConnectionBuffer>,
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
        readable: bool,
        writable: bool,
        capacity: u64,
    ) {
        let flags = match new_connection_validate_flags(
            flags, mode, readable, writable, /*append_allowed=*/ false,
        ) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        let (buffer, was_written) = if flags & OPEN_RIGHT_READABLE == 0 {
            (vec![], false)
        } else if flags & OPEN_FLAG_TRUNCATE != 0 {
            (vec![], true)
        } else {
            let res = match file.clone().init_buffer() {
                AsyncInitBuffer::Immediate(res) => res,
                AsyncInitBuffer::Future(fut) => fut.await,
            };

            match res {
                Err(status) => {
                    send_on_open_with_error(flags, server_end, status);
                    return;
                }
                Ok(buffer) => (buffer, false),
            }
        };

        let (requests, control_handle) =
            match ServerEnd::<FileMarker>::new(server_end.into_channel())
                .into_stream_and_control_handle()
            {
                Ok((requests, control_handle)) => (requests, control_handle),
                Err(_) => {
                    // As we report all errors on `server_end`, if we failed to send an error over
                    // this connection, there is nowhere to send the error to.
                    return;
                }
            };

        if flags & OPEN_FLAG_DESCRIBE != 0 {
            let mut info = NodeInfo::File(FileObject { event: None, stream: None });
            match control_handle.send_on_open_(Status::OK.into_raw(), Some(&mut info)) {
                Ok(()) => (),
                Err(_) => return,
            }
        }

        let handle_requests = FileConnection {
            scope: scope.clone(),
            file,
            requests,
            flags,
            readable,
            writable,
            seek: 0,
            capacity,
            buffer,
            was_written,
        }
        .handle_requests();
        handle_requests.await;
    }

    async fn handle_requests(mut self) {
        while let Some(request_or_err) = self.requests.next().await {
            let state = match request_or_err {
                Err(_) => {
                    // FIDL level error, such as invalid message format and alike.  Close the
                    // connection on any unexpected error.
                    // TODO: Send an epitaph.
                    ConnectionState::Dropped
                }
                Ok(request) => {
                    self.handle_request(request)
                        .await
                        // Protocol level error.  Close the connection on any unexpected error.
                        // TODO: Send an epitaph.
                        .unwrap_or(ConnectionState::Dropped)
                }
            };

            match state {
                ConnectionState::Alive => (),
                ConnectionState::Closed => {
                    // We have already called `handle_close`, do not call it again.
                    return;
                }
                ConnectionState::Dropped => break,
            }
        }

        // If the connection has been closed by the peer or due some error we still need to call
        // the `updated` callback, unless the `Close` message have been used.
        // `ConnectionState::Closed` is handled above.
        let _ = self.handle_close(|_status| Ok(())).await;
    }

    /// POSIX protection attributes are hard coded, as we are expecting them to be removed from the
    /// io.fidl altogether.
    fn posix_protection_attributes(&self) -> u32 {
        match (self.readable, self.writable) {
            (true, true) => POSIX_READ_WRITE_PROTECTION_ATTRIBUTES,
            (true, false) => POSIX_READ_ONLY_PROTECTION_ATTRIBUTES,
            (false, true) => POSIX_WRITE_ONLY_PROTECTION_ATTRIBUTES,
            (false, false) => 0,
        }
    }

    /// Handle a [`FileRequest`]. This function is responsible for handing all the file operations
    /// that operate on the connection-specific buffer.
    // TODO(fxbug.dev/37419): Remove default handling after methods landed.
    #[allow(unreachable_patterns)]
    async fn handle_request(&mut self, req: FileRequest) -> Result<ConnectionState, Error> {
        match req {
            FileRequest::Clone { flags, object, control_handle: _ } => {
                self.handle_clone(self.flags, flags, object);
            }
            FileRequest::Close { responder } => {
                // We are going to close the connection anyways, so there is no way to handle this
                // error.  TODO We may want to send it in an epitaph.
                let _ = self.handle_close(|status| responder.send(status.into_raw())).await;
                return Ok(ConnectionState::Closed);
            }
            FileRequest::Describe { responder } => {
                let mut info = NodeInfo::File(FileObject { event: None, stream: None });
                responder.send(&mut info)?;
            }
            FileRequest::Sync { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            FileRequest::GetAttr { responder } => {
                let size = self.buffer.len() as u64;
                let mut attrs = NodeAttributes {
                    mode: MODE_TYPE_FILE | self.posix_protection_attributes(),
                    id: INO_UNKNOWN,
                    content_size: size,
                    storage_size: size,
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
                responder.send(ZX_OK, self.flags & GET_FLAGS_VISIBLE)?;
            }
            FileRequest::SetFlags { flags: _, responder } => {
                // TODO: Support OPEN_FLAG_APPEND?  It is the only flag that is allowed to be set
                // via this call according to the io.fidl. It would be nice to have that explicitly
                // encoded in the API instead, I guess.
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            FileRequest::GetBuffer { flags: _, responder } => {
                // There is no backing VMO.
                responder.send(ZX_OK, None)?;
            }
            _ => {}
        }
        Ok(ConnectionState::Alive)
    }

    fn handle_clone(&mut self, parent_flags: u32, flags: u32, server_end: ServerEnd<NodeMarker>) {
        let flags = match inherit_rights_for_clone(parent_flags, flags) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        Self::create_connection(
            self.scope.clone(),
            self.file.clone(),
            flags,
            0,
            server_end,
            self.readable,
            self.writable,
            self.capacity,
        );
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
            // This should return Status::OUT_OF_RANGE but POSIX wants an OK. See fxbug.dev/33425.
            responder(Status::OK, &[])?;
            return Ok(0);
        }

        count = core::cmp::min(count, len - offset);

        let from = offset as usize;
        let to = (offset + count) as usize;
        responder(Status::OK, &self.buffer[from..to])?;
        Ok(count)
    }

    /// Write `content` at the current seek position in the buffer associated with the connection.
    /// The corresponding pseudo file should have a size `capacity`. On a successful write, the
    /// number of bytes written is sent to `responder` and also returned from this function. The seek
    /// position is increased by the number of bytes written. On an error, the error code is sent to
    /// `responder`, and this function returns `Ok(())`. If the responder returns an error, this
    /// function forwards that error back to the caller.
    // Strictly speaking, we do not need to use a callback here, but we do need it in the
    // handle_read() case above, so, for consistency, handle_write() has the same interface.
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
    /// this function forwards that error back to the caller.
    // Strictly speaking, we do not need to use a callback here, but we do need it in the
    // handle_read_at() case above, so, for consistency, handle_write_at() has the same interface.
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
    /// returns an error, this function forwards that error back to the caller.
    fn handle_seek<R>(
        &mut self,
        offset: i64,
        start: SeekOrigin,
        responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, u64) -> Result<(), fidl::Error>,
    {
        if self.flags & OPEN_FLAG_NODE_REFERENCE != 0 {
            responder(Status::ACCESS_DENIED, 0)?;
            return Ok(());
        }

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
    /// and this function returns `Ok(())`. If the responder returns an error, this function forwards
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

    /// Closes the connection, calling the `update` callback with an updated buffer if necessary.
    async fn handle_close<R>(&mut self, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        if !self.was_written {
            return responder(Status::OK);
        }

        let buffer = mem::replace(&mut self.buffer, vec![]);
        let res = match self.file.clone().update(buffer) {
            AsyncUpdate::Immediate(res) => res,
            AsyncUpdate::Future(fut) => fut.await,
        };

        let status = match res {
            Ok(()) => Status::OK,
            Err(status) => status,
        };
        responder(status)
    }
}
