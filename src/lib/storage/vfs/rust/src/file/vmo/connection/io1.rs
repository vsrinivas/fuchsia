// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of an individual connection to a file.

use crate::{
    common::{inherit_rights_for_clone, send_on_open_with_error, GET_FLAGS_VISIBLE},
    execution_scope::ExecutionScope,
    file::common::{
        new_connection_validate_flags, POSIX_READ_ONLY_PROTECTION_ATTRIBUTES,
        POSIX_READ_WRITE_PROTECTION_ATTRIBUTES, POSIX_WRITE_ONLY_PROTECTION_ATTRIBUTES,
    },
    file::vmo::{
        asynchronous::{AsyncFileState, NewVmo},
        connection::{AsyncConsumeVmo, FileConnectionApi},
    },
};

use {
    anyhow::Error,
    fidl::endpoints::{RequestStream, ServerEnd},
    fidl_fuchsia_io::{
        FileObject, FileRequest, FileRequestStream, NodeAttributes, NodeInfo, NodeMarker,
        SeekOrigin, INO_UNKNOWN, MODE_TYPE_FILE, OPEN_FLAG_DESCRIBE, OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_TRUNCATE, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE, VMO_FLAG_EXACT,
        VMO_FLAG_EXEC, VMO_FLAG_PRIVATE, VMO_FLAG_READ, VMO_FLAG_WRITE,
    },
    fidl_fuchsia_mem::Buffer,
    fuchsia_zircon::{
        sys::{ZX_ERR_NOT_SUPPORTED, ZX_OK},
        AsHandleRef, HandleBased, Rights, Status, Vmo, VmoChildOptions,
    },
    futures::{lock::MutexGuard, stream::StreamExt},
    static_assertions::assert_eq_size,
    std::{mem, sync::Arc},
};

macro_rules! update_initialized_state {
    (match $status:expr;
     error: $method_name:expr => $uninitialized_result:expr ;
     { $( $vars:tt ),* $(,)* } => $body:stmt $(;)*) => {
        match $status {
            AsyncFileState::Uninitialized => {
                let name = $method_name;
                debug_assert!(false, "`{}` called for a file with no connections", name);
                $uninitialized_result
            }
            AsyncFileState::Initialized { $( $vars ),* } => loop { break { $body } },
        }
    }
}

/// Represents a FIDL connection to a file.
pub struct FileConnection {
    /// Execution scope this connection and any async operations and connections it creates will
    /// use.
    scope: ExecutionScope,

    /// File this connection is associated with.
    file: Arc<dyn FileConnectionApi>,

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
}

/// Return type for [`handle_request()`] functions.
enum ConnectionState {
    /// Connection is still alive.
    Alive,
    /// Connection have received Node::Close message and the [`handle_close`] method has been
    /// already called for this connection.
    Closed,
    /// Connection has been dropped by the peer or an error has occurred.  [`handle_close`] still
    /// need to be called (though it would not be able to report the status to the peer).
    Dropped,
}

enum SharingMode {
    Shared,
    Private,
}

impl FileConnection {
    /// Initialized a file connection, which will be running in the context of the specified
    /// execution `scope`.  This function will also check the flags and will send the `OnOpen`
    /// event if necessary.
    ///
    /// Per connection buffer is initialized using the `init_vmo` closure, as part of the
    /// connection initialization.
    pub(in crate::file::vmo) fn create_connection(
        scope: ExecutionScope,
        file: Arc<dyn FileConnectionApi>,
        flags: u32,
        mode: u32,
        readable: bool,
        writable: bool,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let task = Self::create_connection_task(
            scope.clone(),
            file,
            flags,
            mode,
            readable,
            writable,
            server_end,
        );
        // If we failed to send the task to the executor, it is probably shut down or is in the
        // process of shutting down (this is the only error state currently).  So there is nothing
        // for us to do, but to ignore the open.  `server_end` will be closed when the object will
        // be dropped - there seems to be no error to report there.
        let _ = scope.spawn(Box::pin(task));
    }

    async fn create_connection_task(
        scope: ExecutionScope,
        file: Arc<dyn FileConnectionApi>,
        flags: u32,
        mode: u32,
        readable: bool,
        writable: bool,
        server_end: ServerEnd<NodeMarker>,
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

        let server_end = {
            let (mut state, server_end) =
                match Self::ensure_vmo(file.clone(), file.state().await, server_end).await {
                    Ok(res) => res,
                    Err((status, server_end)) => {
                        send_on_open_with_error(flags, server_end, status);
                        return;
                    }
                };

            if flags & OPEN_FLAG_TRUNCATE != 0 {
                let mut seek = 0;
                if let Err(status) = Self::truncate_vmo(&mut *state, 0, &mut seek) {
                    send_on_open_with_error(flags, server_end, status);
                    return;
                }
                debug_assert!(seek == 0);
            }

            match &mut *state {
                AsyncFileState::Uninitialized => {
                    debug_assert!(false, "`ensure_vmo` did not initialize the state.");
                    send_on_open_with_error(flags, server_end, Status::INTERNAL);
                    return;
                }
                AsyncFileState::Initialized { connection_count, .. } => {
                    *connection_count += 1;
                }
            }

            server_end
        };

        let (requests, control_handle) = match server_end.into_stream_and_control_handle() {
            Ok((requests, control_handle)) => (requests.cast_stream(), control_handle),
            Err(_) => {
                // As we report all errors on `server_end`, if we failed to send an error over this
                // connection, there is nowhere to send the error to.
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
        }
        .handle_requests();
        handle_requests.await;
    }

    async fn ensure_vmo<'state_guard>(
        file: Arc<dyn FileConnectionApi>,
        mut state: MutexGuard<'state_guard, AsyncFileState>,
        server_end: ServerEnd<NodeMarker>,
    ) -> Result<
        (MutexGuard<'state_guard, AsyncFileState>, ServerEnd<NodeMarker>),
        (Status, ServerEnd<NodeMarker>),
    > {
        if let AsyncFileState::Initialized { .. } = *state {
            return Ok((state, server_end));
        }

        let NewVmo { vmo, mut size, capacity } = match file.init_vmo().await {
            Ok(res) => res,
            Err(status) => return Err((status, server_end)),
        };
        let mut vmo_size = match vmo.get_size() {
            Ok(size) => size,
            Err(status) => return Err((status, server_end)),
        };

        if cfg!(debug_assertions) {
            // Debug build will just enforce the constraints.
            assert!(
                vmo_size >= size,
                "`init_vmo` returned a VMO that is smaller than the declared size.\n\
                 VMO size: {}\n\
                 Declared size: {}",
                vmo_size,
                size
            );
        } else if vmo_size < size {
            // Release build will try to recover.
            match vmo.set_size(size) {
                Ok(()) => {
                    // Actual VMO size might be different from the requested one due to rounding,
                    // so we have to ask for it.
                    vmo_size = match vmo.get_size() {
                        Ok(size) => size,
                        Err(status) => return Err((status, server_end)),
                    };
                }
                Err(Status::UNAVAILABLE) => {
                    // VMO is not resizable.  Try to use what we got.
                    size = vmo_size;
                }
                Err(status) => return Err((status, server_end)),
            }
        }

        *state = AsyncFileState::Initialized {
            vmo,
            vmo_size,
            size,
            capacity,
            // We are going to increment the connection count later, so it needs to
            // start at 0.
            connection_count: 0,
        };

        Ok((state, server_end))
    }

    fn truncate_vmo(
        state: &mut AsyncFileState,
        new_size: u64,
        seek: &mut u64,
    ) -> Result<(), Status> {
        update_initialized_state! {
            match state;
            error: "truncate_vmo" => Err(Status::INTERNAL);
            { vmo, vmo_size, size, capacity, .. } => {
                let effective_capacity = core::cmp::max(*size, *capacity);

                if new_size > effective_capacity {
                    break Err(Status::OUT_OF_RANGE);
                }

                assert_eq_size!(usize, u64);

                vmo.set_size(new_size)?;
                // Actual VMO size might be different from the requested one due to rounding,
                // so we have to ask for it.
                *vmo_size = match vmo.get_size() {
                    Ok(size) => size,
                    Err(status) => break Err(status),
                };

                *size = new_size;

                // We are not supposed to touch the seek position during truncation, but the
                // effective_capacity might be smaller now - in which case we do need to move the
                // seek position.
                let new_effective_capacity = core::cmp::max(new_size, *capacity);
                *seek = core::cmp::min(*seek, new_effective_capacity);

                Ok(())
            }
        }
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
    async fn handle_request(&mut self, req: FileRequest) -> Result<ConnectionState, Error> {
        // TODO(fxbug.dev/37419): Remove `allow(unreachable_patterns)` when the bug is done.
        #[allow(unreachable_patterns)]
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
                // VMOs are always in sync.
                responder.send(ZX_OK)?;
            }
            FileRequest::GetAttr { responder } => {
                self.handle_get_attr(|status, mut attrs| {
                    responder.send(status.into_raw(), &mut attrs)
                })
                .await?;
            }
            FileRequest::SetAttr { flags: _, attributes: _, responder } => {
                // According to zircon/system/fidl/fuchsia-io/io.fidl the only flag that might be
                // modified through this call is OPEN_FLAG_APPEND, and it is not supported at the
                // moment.
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            FileRequest::Read { count, responder } => {
                self.handle_read(count, |status, content| {
                    responder.send(status.into_raw(), content)
                })
                .await?;
            }
            FileRequest::ReadAt { offset, count, responder } => {
                self.handle_read_at(offset, count, |status, content| {
                    responder.send(status.into_raw(), content)
                })
                .await?;
            }
            FileRequest::Write { data, responder } => {
                self.handle_write(&data, |status, actual| {
                    responder.send(status.into_raw(), actual)
                })
                .await?;
            }
            FileRequest::WriteAt { offset, data, responder } => {
                self.handle_write_at(offset, &data, |status, actual| {
                    // Seems like our API is not really designed for 128 bit machines. If data
                    // contains more than 16EB, we may not be returning the correct number here.
                    responder.send(status.into_raw(), actual as u64)
                })
                .await?;
            }
            FileRequest::Seek { offset, start, responder } => {
                self.handle_seek(offset, start, |status, offset| {
                    responder.send(status.into_raw(), offset)
                })
                .await?;
            }
            FileRequest::Truncate { length, responder } => {
                self.handle_truncate(length, |status| responder.send(status.into_raw())).await?;
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
            FileRequest::GetBuffer { flags, responder } => {
                self.handle_get_buffer(flags, |status, buffer| match buffer {
                    None => responder.send(status.into_raw(), None),
                    Some(mut buffer) => responder.send(status.into_raw(), Some(&mut buffer)),
                })
                .await?;
            }
            // TODO(fxbug.dev/37419): Remove default handling after methods landed.
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
            self.readable,
            self.writable,
            server_end,
        );
    }

    /// Closes the connection, calling the `consume_vmo` callback with an updated buffer if
    /// necessary.
    async fn handle_close<R>(&mut self, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        let (status, async_consume_task) = {
            let state = &mut *self.file.state().await;
            match state {
                AsyncFileState::Uninitialized => {
                    debug_assert!(false, "`handle_close` called for a file with no connections");
                    (Status::INTERNAL, None)
                }
                AsyncFileState::Initialized { connection_count: 1, .. } => {
                    match mem::replace(state, AsyncFileState::Uninitialized) {
                        AsyncFileState::Uninitialized => unreachable!(),
                        AsyncFileState::Initialized { vmo, .. } => {
                            (Status::OK, Some(self.file.clone().consume_vmo(vmo)))
                        }
                    }
                }
                AsyncFileState::Initialized { connection_count, .. } => {
                    *connection_count -= 1;

                    (Status::OK, None)
                }
            }
        };

        // Call responder ASAP, as `consume_vmo` might take some time (even though it should not).
        let responder_res = responder(status);

        if let Some(async_consume_task) = async_consume_task {
            match async_consume_task {
                AsyncConsumeVmo::Immediate(()) => (),
                AsyncConsumeVmo::Future(fut) => {
                    // If we failed to send the task to the executor, it is probably shut down or
                    // is in the process of shutting down (this is the only error state currently).
                    // So there is nothing for us to do, but to ignore the open.  `server_end` will
                    // be closed when the object will be dropped - there seems to be no error to
                    // report there.
                    let _ = self.scope.spawn(fut);
                }
            };
        }

        responder_res
    }

    async fn handle_get_attr<R>(&mut self, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, NodeAttributes) -> Result<(), fidl::Error>,
    {
        let (status, size, capacity) = update_initialized_state! {
            match *self.file.state().await;
            error: "handle_get_attr" => (Status::INTERNAL, 0, 0);
            { size, capacity, .. } => (Status::OK, size, capacity)
        };

        responder(
            status,
            NodeAttributes {
                mode: MODE_TYPE_FILE | self.posix_protection_attributes(),
                id: INO_UNKNOWN,
                content_size: size,
                storage_size: capacity,
                link_count: 1,
                creation_time: 0,
                modification_time: 0,
            },
        )
    }

    /// Read `count` bytes at the current seek value from the underlying VMO.  The content is sent
    /// as an iterator to the provided responder. It increases the current seek position by the
    /// actual number of bytes written. If an error occurs, an error code is sent to the responder
    /// with an empty iterator, and this function returns `Ok(())`. If the responder returns an
    /// error when used (including in the successful case), this function returns that error
    /// directly.
    async fn handle_read<R>(&mut self, count: u64, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, &[u8]) -> Result<(), fidl::Error>,
    {
        let actual = self.handle_read_at(self.seek, count, responder).await?;
        self.seek += actual;
        Ok(())
    }

    /// Read `count` bytes at `offset` from the underlying VMO. The content is sent as an iterator
    /// to the provided responder. If an error occurs, an error code is sent to the responder with
    /// an empty iterator, and this function returns `Ok(0)`. If the responder returns an error
    /// when used (including in the successful case), this function returns that error directly.
    async fn handle_read_at<R>(
        &mut self,
        offset: u64,
        count: u64,
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

        let (status, count, content) = update_initialized_state! {
            match &*self.file.state().await;
            error: "handle_read_at" => (Status::INTERNAL, 0, vec![]);
            { vmo, size, .. } => {
                if offset >= *size {
                    // This should return Status::OUT_OF_RANGE but POSIX wants an OK. See fxbug.dev/33425.
                    break (Status::OK, 0, vec![]);
                }

                let count = core::cmp::min(count, *size - offset);

                let mut buffer = Vec::with_capacity(count as usize);
                buffer.resize(count as usize, 0);

                match vmo.read(&mut buffer, offset) {
                    Ok(()) => (Status::OK, count, buffer),
                    Err(status) => (status, 0, vec![]),
                }
            }
        };

        responder(status, &content)?;
        Ok(count)
    }

    /// Write `content` at the current seek position into the underlying VMO.  The file should have
    /// enough `capacity` to cover all the bytes that are written.  When not enough bytes are
    /// available only the overlap is written.  When the seek position is already beyond the
    /// `capacity`, an `OUT_OF_RANGE` error is provided to the `responder`.  The seek position is
    /// increased by the number of bytes written. On an error, the error code is sent to
    /// `responder`, and this function returns `Ok(())`. If the responder returns an error, this
    /// function forwards that error back to the caller.
    // Strictly speaking, we do not need to use a callback here, but we do need it in the
    // handle_read() case above, so, for consistency, handle_write() has the same interface.
    async fn handle_write<R>(&mut self, content: &[u8], responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, u64) -> Result<(), fidl::Error>,
    {
        let actual = self.handle_write_at(self.seek, content, responder).await?;
        self.seek += actual;
        Ok(())
    }

    /// Write `content` at `offset` in the buffer associated with the connection. The file should
    /// have enough `capacity` to cover all the bytes that are written.  When not enough bytes are
    /// available only the overlap is written.  When `offset` is beyond the `capacity`, an
    /// `OUT_OF_RANGE` error is provided to the `responder`. On a successful write, the number of
    /// bytes written is sent to `responder` and also returned from this function. On an error, the
    /// error code is sent to `responder`, and this function returns `Ok(0)`. If the responder
    /// returns an error, this function forwards that error back to the caller.
    // Strictly speaking, we do not need to use a callback here, but we do need it in the
    // handle_read_at() case above, so, for consistency, handle_write_at() has the same interface.
    async fn handle_write_at<R>(
        &mut self,
        offset: u64,
        mut content: &[u8],
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

        let (status, actual) = update_initialized_state! {
            match &mut *self.file.state().await;
            error: "handle_write_at" => (Status::INTERNAL, 0);
            { vmo, vmo_size, size, capacity, .. } => {
                let effective_capacity = core::cmp::max(*size, *capacity);

                if offset >= effective_capacity {
                    break (Status::OUT_OF_RANGE, 0);
                }

                let mut actual = content.len() as u64;
                if effective_capacity - offset < actual {
                    actual = effective_capacity - offset;
                    content = &content[0..actual as usize];
                }

                if *size <= offset + actual {
                    let new_size = offset + actual;
                    if *vmo_size < new_size {
                        if let Err(status) = vmo.set_size(new_size) {
                            break (status, 0);
                        }
                        // As VMO sizes are rounded, we do not really know the current size of the
                        // VMO after the `set_size` call.  We need an additional `get_size`, if we
                        // want to be aware of the exact size.  We can probably do our own
                        // rounding, but it seems more fragile.  Hopefully, this extra syscall will
                        // be invisible, as it should not happen too frequently.  It will be at
                        // least offset by 4 more syscalls that happen for every `write_at` FIDL
                        // call.
                        *vmo_size = match vmo.get_size() {
                            Ok(size) => size,
                            Err(status) => break (status, 0),
                        };
                    }
                    *size = offset + actual;
                }

                match vmo.write(&content, offset) {
                    Ok(()) => (Status::OK, actual),
                    Err(status) => (status, 0),
                }
            }
        };

        responder(status, actual)?;
        Ok(actual)
    }

    /// Move seek position to byte `offset` relative to the origin specified by `start.  Calls
    /// `responder` with an updated seek position, on success.
    async fn handle_seek<R>(
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

        let (status, seek) = update_initialized_state! {
            match *self.file.state().await;
            error: "handle_seek" => (Status::INTERNAL, 0);
            { size, capacity, .. } => {
                let new_seek = match start {
                    SeekOrigin::Start => offset as i128,

                    SeekOrigin::Current => {
                        assert_eq_size!(usize, i64);
                        self.seek as i128 + offset as i128
                    }

                    SeekOrigin::End => {
                        assert_eq_size!(usize, i64, u64);
                        size as i128 + offset as i128
                    }
                };

                let effective_capacity = core::cmp::max(size as i128, capacity as i128);
                if new_seek < 0 || new_seek >= effective_capacity {
                    break (Status::OUT_OF_RANGE, self.seek);
                }
                let new_seek = new_seek as u64;

                self.seek = new_seek;
                (Status::OK, new_seek)
            }
        };

        responder(status, seek)?;
        Ok(())
    }

    /// Truncate to `length` the buffer associated with the connection. The corresponding pseudo
    /// file should have a size `capacity`. If after the truncation the seek position would be
    /// beyond the new end of the buffer, it is set to the end of the buffer. On a successful
    /// truncate, [`Status::OK`] is sent to `responder`. On an error, the error code is sent to
    /// `responder`, and this function returns `Ok(())`. If the responder returns an error, this
    /// function forwards that error back to the caller.
    async fn handle_truncate<R>(&mut self, length: u64, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        if self.flags & OPEN_RIGHT_WRITABLE == 0 {
            return responder(Status::ACCESS_DENIED);
        }

        match Self::truncate_vmo(&mut *self.file.state().await, length, &mut self.seek) {
            Ok(()) => responder(Status::OK),
            Err(status) => responder(status),
        }
    }

    async fn handle_get_buffer<R>(&mut self, flags: u32, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, Option<Buffer>) -> Result<(), fidl::Error>,
    {
        let mode = match Self::get_buffer_validate_flags(flags, self.flags) {
            Err(status) => return responder(status, None),
            Ok(mode) => mode,
        };

        let (status, buffer) = update_initialized_state! {
            match &*self.file.state().await;
            error: "handle_write_at" => (Status::INTERNAL, None);
            { vmo, size, .. } => match mode {
                // Logic here matches the io.fidl requirements and matches what works for memfs.
                // Shared requests are satisfied by duplicating an handle, and private shares are
                // child VMOs.
                //
                // Minfs and blobfs may require customization.  In particular, they may want to
                // track not just number of connections to a file, but also the number of
                // outstanding child VMOs.  While it is possible with the `init_vmo`/`consume_vmo`
                // model currently implemented, it is very likely that adding another customization
                // callback here will make the implementation of those files systems easier.
                SharingMode::Shared => match Self::get_as_shared(&vmo, flags) {
                    Ok(vmo) => (Status::OK, Some(Buffer { vmo, size: *size })),
                    Err(status) => (status, None),
                },
                SharingMode::Private => match Self::get_as_private(&vmo, flags, *size) {
                    Ok(vmo) => (Status::OK, Some(Buffer { vmo, size: *size })),
                    Err(status) => (status, None),
                },
            }
        };

        responder(status, buffer)
    }

    fn get_vmo_rights(vmo: &Vmo, flags: u32) -> Result<Rights, Status> {
        let mut rights = vmo.basic_info()?.rights;

        if flags & VMO_FLAG_READ == 0 {
            rights -= Rights::READ;
        }

        if flags & VMO_FLAG_WRITE == 0 {
            rights -= Rights::WRITE;
        }

        if flags & VMO_FLAG_EXEC == 0 {
            rights -= Rights::EXECUTE;
        }

        Ok(rights)
    }

    fn get_as_shared(vmo: &Vmo, flags: u32) -> Result<Vmo, Status> {
        let rights = Self::get_vmo_rights(vmo, flags)?;
        vmo.as_handle_ref().duplicate(rights).map(Into::into)
    }

    fn get_as_private(vmo: &Vmo, flags: u32, size: u64) -> Result<Vmo, Status> {
        let new_vmo =
            vmo.create_child(VmoChildOptions::COPY_ON_WRITE | VmoChildOptions::RESIZABLE, 0, size)?;
        let rights = Self::get_vmo_rights(vmo, flags)?;
        new_vmo.into_handle().replace_handle(rights).map(Into::into)
    }

    fn get_buffer_validate_flags(
        new_vmo_flags: u32,
        connection_flags: u32,
    ) -> Result<SharingMode, Status> {
        if connection_flags & OPEN_RIGHT_READABLE == 0
            && (new_vmo_flags & VMO_FLAG_READ != 0 || new_vmo_flags & VMO_FLAG_EXEC != 0)
        {
            return Err(Status::ACCESS_DENIED);
        }

        if connection_flags & OPEN_RIGHT_WRITABLE == 0 && new_vmo_flags & VMO_FLAG_WRITE != 0 {
            return Err(Status::ACCESS_DENIED);
        }

        if new_vmo_flags & VMO_FLAG_PRIVATE != 0 && new_vmo_flags & VMO_FLAG_EXACT != 0 {
            return Err(Status::INVALID_ARGS);
        }

        // We do not share the VMO itself with a WRITE flag, as this would allow someone to change
        // the size "under our feel" and there seems to be now way to protect from it.
        if new_vmo_flags & VMO_FLAG_EXACT != 0 && new_vmo_flags & VMO_FLAG_WRITE != 0 {
            return Err(Status::NOT_SUPPORTED);
        }

        // We use shared mode by default, if the caller did not specify.  It should be more
        // lightweight, I assume?  Except when a writable share is necessary.  `VMO_FLAG_EXACT |
        // VMO_FLAG_WRITE` is prohibited above.
        if new_vmo_flags & VMO_FLAG_PRIVATE != 0 || new_vmo_flags & VMO_FLAG_WRITE != 0 {
            Ok(SharingMode::Private)
        } else {
            Ok(SharingMode::Shared)
        }
    }
}
