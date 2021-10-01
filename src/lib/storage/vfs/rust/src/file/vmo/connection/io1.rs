// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Implementation of an individual connection to a file.

use crate::{
    common::{
        inherit_rights_for_clone, rights_to_posix_mode_bits, send_on_open_with_error,
        GET_FLAGS_VISIBLE,
    },
    execution_scope::ExecutionScope,
    file::common::{get_buffer_validate_flags, new_connection_validate_flags, vmo_flags_to_rights},
    file::vmo::{
        asynchronous::{NewVmo, VmoFileState},
        connection::{AsyncConsumeVmo, VmoFileInterface},
    },
};

use {
    anyhow::Error,
    fidl::endpoints::ServerEnd,
    fidl::prelude::*,
    fidl_fuchsia_io::{
        FileObject, FileRequest, FileRequestStream, NodeAttributes, NodeInfo, NodeMarker,
        SeekOrigin, Vmofile, INO_UNKNOWN, MODE_TYPE_FILE, OPEN_FLAG_DESCRIBE,
        OPEN_FLAG_NODE_REFERENCE, OPEN_FLAG_TRUNCATE, OPEN_RIGHT_EXECUTABLE, OPEN_RIGHT_READABLE,
        OPEN_RIGHT_WRITABLE, VMO_FLAG_EXEC, VMO_FLAG_PRIVATE, VMO_FLAG_WRITE,
    },
    fidl_fuchsia_mem::Buffer,
    fuchsia_zircon::{
        self as zx,
        sys::{ZX_ERR_NOT_SUPPORTED, ZX_OK},
        AsHandleRef, HandleBased,
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
            VmoFileState::Uninitialized => {
                let name = $method_name;
                debug_assert!(false, "`{}` called for a file with no connections", name);
                $uninitialized_result
            }
            VmoFileState::Initialized { $( $vars ),* } => loop { break { $body } },
        }
    }
}

/// Represents a FIDL connection to a file.
pub struct VmoFileConnection {
    /// Execution scope this connection and any async operations and connections it creates will
    /// use.
    scope: ExecutionScope,

    /// File this connection is associated with.
    file: Arc<dyn VmoFileInterface>,

    /// Wraps a FIDL connection, providing messages coming from the client.
    requests: FileRequestStream,

    /// Either the "flags" value passed into [`DirectoryEntry::open()`], or the "flags" value
    /// received with [`FileRequest::Clone()`].
    flags: u32,

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

impl VmoFileConnection {
    /// Initialized a file connection, which will be running in the context of the specified
    /// execution `scope`.  This function will also check the flags and will send the `OnOpen`
    /// event if necessary.
    ///
    /// Per connection buffer is initialized using the `init_vmo` closure, as part of the
    /// connection initialization.
    pub(in crate::file::vmo) fn create_connection(
        scope: ExecutionScope,
        file: Arc<dyn VmoFileInterface>,
        flags: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let task = Self::create_connection_task(scope.clone(), file, flags, server_end);
        // If we failed to send the task to the executor, it is probably shut down or is in the
        // process of shutting down (this is the only error state currently).  So there is nothing
        // for us to do, but to ignore the open.  `server_end` will be closed when the object will
        // be dropped - there seems to be no error to report there.
        let _ = scope.spawn(Box::pin(task));
    }

    async fn create_connection_task(
        scope: ExecutionScope,
        file: Arc<dyn VmoFileInterface>,
        flags: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let flags = match new_connection_validate_flags(
            flags,
            file.is_readable(),
            file.is_writable(),
            file.is_executable(),
            /*append_allowed=*/ false,
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
                VmoFileState::Uninitialized => {
                    debug_assert!(false, "`ensure_vmo` did not initialize the state.");
                    send_on_open_with_error(flags, server_end, zx::Status::INTERNAL);
                    return;
                }
                VmoFileState::Initialized { connection_count, .. } => {
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

        let mut connection =
            VmoFileConnection { scope: scope.clone(), file, requests, flags, seek: 0 };

        if flags & OPEN_FLAG_DESCRIBE != 0 {
            match connection.get_node_info().await {
                Ok(mut info) => {
                    let send_result =
                        control_handle.send_on_open_(zx::Status::OK.into_raw(), Some(&mut info));
                    if send_result.is_err() {
                        return;
                    }
                }
                Err(status) => {
                    debug_assert!(status != zx::Status::OK);
                    control_handle.shutdown_with_epitaph(status);
                    return;
                }
            }
        }

        connection.handle_requests().await;
    }

    async fn ensure_vmo<'state_guard>(
        file: Arc<dyn VmoFileInterface>,
        mut state: MutexGuard<'state_guard, VmoFileState>,
        server_end: ServerEnd<NodeMarker>,
    ) -> Result<
        (MutexGuard<'state_guard, VmoFileState>, ServerEnd<NodeMarker>),
        (zx::Status, ServerEnd<NodeMarker>),
    > {
        if let VmoFileState::Initialized { .. } = *state {
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
                Err(zx::Status::UNAVAILABLE) => {
                    // VMO is not resizable.  Try to use what we got.
                    size = vmo_size;
                }
                Err(status) => return Err((status, server_end)),
            }
        }

        *state = VmoFileState::Initialized {
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
        state: &mut VmoFileState,
        new_size: u64,
        seek: &mut u64,
    ) -> Result<(), zx::Status> {
        update_initialized_state! {
            match state;
            error: "truncate_vmo" => Err(zx::Status::INTERNAL);
            { vmo, vmo_size, size, capacity, .. } => {
                let effective_capacity = core::cmp::max(*size, *capacity);

                if new_size > effective_capacity {
                    break Err(zx::Status::OUT_OF_RANGE);
                }

                assert_eq_size!(usize, u64);

                vmo.set_size(new_size)?;
                // Actual VMO size might be different from the requested one due to rounding,
                // so we have to ask for it.
                *vmo_size = match vmo.get_size() {
                    Ok(size) => size,
                    Err(status) => break Err(status),
                };

                    if let Err(status) = vmo.set_content_size(&new_size) {
                        break Err(status);
                    }

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

    /// Returns `NodeInfo` for the VMO file.
    async fn get_node_info(&mut self) -> Result<NodeInfo, zx::Status> {
        // The current io.fidl specification for Vmofile node types specify that the node is
        // immutable, thus if the file is writable, we report it as a regular file instead.
        // If this changes in the future, we need to handle size changes in the backing VMO.
        if self.flags & OPEN_FLAG_NODE_REFERENCE != 0 || self.flags & OPEN_RIGHT_WRITABLE != 0 {
            Ok(NodeInfo::File(FileObject { event: None, stream: None }))
        } else {
            let vmofile = update_initialized_state! {
                match &*self.file.state().await;
                error: "get_node_info" => Err(zx::Status::INTERNAL);
                { vmo, size, .. } => {
                    // Since the VMO rights may exceed those of the connection, we need to ensure
                    // the duplicated handle's rights are not greater than those of the connection.
                    let mut new_rights = vmo.basic_info().unwrap().rights;
                    // We already checked above that the connection is not writable. We also remove
                    // SET_PROPERTY as this would also allow size changes.
                    new_rights.remove(zx::Rights::WRITE | zx::Rights::SET_PROPERTY);
                    if (self.flags & OPEN_RIGHT_EXECUTABLE) == 0 {
                        new_rights.remove(zx::Rights::EXECUTE);
                    }
                    let vmo = vmo.duplicate_handle(new_rights).unwrap();

                    Ok(Vmofile {vmo, offset: 0, length: *size})
                }
            }?;
            Ok(NodeInfo::Vmofile(vmofile))
        }
    }

    /// Handle a [`FileRequest`]. This function is responsible for handing all the file operations
    /// that operate on the connection-specific buffer.
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
            FileRequest::Close2 { responder } => {
                // We are going to close the connection anyways, so there is no way to handle this
                // error.
                let _ = self
                    .handle_close(|status| {
                        let result: Result<(), zx::Status> = status.into();
                        responder.send(&mut result.map_err(|status| status.into_raw()))
                    })
                    .await;
                return Ok(ConnectionState::Closed);
            }
            FileRequest::Describe { responder } => match self.get_node_info().await {
                Ok(mut info) => responder.send(&mut info)?,
                Err(status) => {
                    debug_assert!(status != zx::Status::OK);
                    responder.control_handle().shutdown_with_epitaph(status);
                }
            },
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
            FileRequest::NodeGetFlags { responder } => {
                responder.send(ZX_OK, self.flags & GET_FLAGS_VISIBLE)?;
            }
            FileRequest::NodeSetFlags { flags: _, responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            FileRequest::AdvisoryLock { request: _, responder } => {
                responder.send(&mut Err(ZX_ERR_NOT_SUPPORTED))?;
            }
            // TODO(https://fxbug.dev/77623): Remove when the io1 -> io2 transition is complete.
            _ => panic!("Unhandled request!"),
        }
        Ok(ConnectionState::Alive)
    }

    fn handle_clone(
        &mut self,
        parent_flags: u32,
        current_flags: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let flags = match inherit_rights_for_clone(parent_flags, current_flags) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(current_flags, server_end, status);
                return;
            }
        };

        Self::create_connection(self.scope.clone(), self.file.clone(), flags, server_end);
    }

    /// Closes the connection, calling the `consume_vmo` callback with an updated buffer if
    /// necessary.
    async fn handle_close<R>(&mut self, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(zx::Status) -> Result<(), fidl::Error>,
    {
        let (status, async_consume_task) = {
            let state = &mut *self.file.state().await;
            match state {
                VmoFileState::Uninitialized => {
                    debug_assert!(false, "`handle_close` called for a file with no connections");
                    (zx::Status::INTERNAL, None)
                }
                VmoFileState::Initialized { connection_count: 1, .. } => {
                    match mem::replace(state, VmoFileState::Uninitialized) {
                        VmoFileState::Uninitialized => unreachable!(),
                        VmoFileState::Initialized { vmo, .. } => {
                            (zx::Status::OK, Some(self.file.clone().consume_vmo(vmo)))
                        }
                    }
                }
                VmoFileState::Initialized { connection_count, .. } => {
                    *connection_count -= 1;

                    (zx::Status::OK, None)
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
        R: FnOnce(zx::Status, NodeAttributes) -> Result<(), fidl::Error>,
    {
        let (status, size, capacity) = update_initialized_state! {
            match *self.file.state().await;
            error: "handle_get_attr" => (zx::Status::INTERNAL, 0, 0);
            { size, capacity, .. } => (zx::Status::OK, size, capacity)
        };

        responder(
            status,
            NodeAttributes {
                mode: MODE_TYPE_FILE
                    | rights_to_posix_mode_bits(
                        self.file.is_readable(),
                        self.file.is_writable(),
                        self.file.is_executable(),
                    ),
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
        R: FnOnce(zx::Status, &[u8]) -> Result<(), fidl::Error>,
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
        R: FnOnce(zx::Status, &[u8]) -> Result<(), fidl::Error>,
    {
        if self.flags & OPEN_RIGHT_READABLE == 0 {
            responder(zx::Status::BAD_HANDLE, &[])?;
            return Ok(0);
        }

        assert_eq_size!(usize, u64);

        let (status, count, content) = update_initialized_state! {
            match &*self.file.state().await;
            error: "handle_read_at" => (zx::Status::INTERNAL, 0, vec![]);
            { vmo, size, .. } => {
                if offset >= *size {
                    // This should return Status::OUT_OF_RANGE but POSIX wants an OK.
                    // See fxbug.dev/33425.
                    break (zx::Status::OK, 0, vec![]);
                }

                let count = core::cmp::min(count, *size - offset);

                let mut buffer = Vec::with_capacity(count as usize);
                buffer.resize(count as usize, 0);

                match vmo.read(&mut buffer, offset) {
                    Ok(()) => (zx::Status::OK, count, buffer),
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
        R: FnOnce(zx::Status, u64) -> Result<(), fidl::Error>,
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
        R: FnOnce(zx::Status, u64) -> Result<(), fidl::Error>,
    {
        if self.flags & OPEN_RIGHT_WRITABLE == 0 {
            responder(zx::Status::BAD_HANDLE, 0)?;
            return Ok(0);
        }

        assert_eq_size!(usize, u64);

        let (status, actual) = update_initialized_state! {
            match &mut *self.file.state().await;
            error: "handle_write_at" => (zx::Status::INTERNAL, 0);
            { vmo, vmo_size, size, capacity, .. } => {
                let effective_capacity = core::cmp::max(*size, *capacity);

                if offset >= effective_capacity {
                    break (zx::Status::OUT_OF_RANGE, 0);
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
                    if let Err(status) = vmo.set_content_size(&new_size) {
                        break (status, 0);
                    }
                    *size = offset + actual;
                }

                match vmo.write(&content, offset) {
                    Ok(()) => (zx::Status::OK, actual),
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
        R: FnOnce(zx::Status, u64) -> Result<(), fidl::Error>,
    {
        if self.flags & OPEN_FLAG_NODE_REFERENCE != 0 {
            responder(zx::Status::BAD_HANDLE, 0)?;
            return Ok(());
        }

        let (status, seek) = update_initialized_state! {
            match *self.file.state().await;
            error: "handle_seek" => (zx::Status::INTERNAL, 0);
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
                    break (zx::Status::OUT_OF_RANGE, self.seek);
                }
                let new_seek = new_seek as u64;

                self.seek = new_seek;
                (zx::Status::OK, new_seek)
            }
        };

        responder(status, seek)?;
        Ok(())
    }

    /// Truncate to `length` the buffer associated with the connection. The corresponding pseudo
    /// file should have a size `capacity`. If after the truncation the seek position would be
    /// beyond the new end of the buffer, it is set to the end of the buffer. On a successful
    /// truncate, [`zx::Status::OK`] is sent to `responder`. On an error, the error code is sent to
    /// `responder`, and this function returns `Ok(())`. If the responder returns an error, this
    /// function forwards that error back to the caller.
    async fn handle_truncate<R>(&mut self, length: u64, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(zx::Status) -> Result<(), fidl::Error>,
    {
        if self.flags & OPEN_RIGHT_WRITABLE == 0 {
            return responder(zx::Status::BAD_HANDLE);
        }

        match Self::truncate_vmo(&mut *self.file.state().await, length, &mut self.seek) {
            Ok(()) => responder(zx::Status::OK),
            Err(status) => responder(status),
        }
    }

    async fn handle_get_buffer<R>(&mut self, flags: u32, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(zx::Status, Option<Buffer>) -> Result<(), fidl::Error>,
    {
        if let Err(status) = get_buffer_validate_flags(flags, self.flags) {
            return responder(status, None);
        }

        // The only sharing mode we support that disallows the VMO size to change currently
        // is VMO_FLAG_PRIVATE (`get_as_private`), so we require that to be set explicitly.
        if flags & VMO_FLAG_WRITE != 0 && flags & VMO_FLAG_PRIVATE == 0 {
            return responder(zx::Status::NOT_SUPPORTED, None);
        }

        // Disallow opening as both writable and executable. In addition to improving W^X
        // enforcement, this also eliminates any inconstiencies related to clones that use
        // SNAPSHOT_AT_LEAST_ON_WRITE since in that case, we cannot satisfy both requirements.
        if flags & VMO_FLAG_EXEC != 0 && flags & VMO_FLAG_WRITE != 0 {
            return responder(zx::Status::NOT_SUPPORTED, None);
        }

        let (status, buffer) = update_initialized_state! {
            match &*self.file.state().await;
            error: "handle_write_at" => (zx::Status::INTERNAL, None);
            { vmo, size, .. } => {
                // Logic here matches the io.fidl requirements and matches what works for memfs.
                // Shared requests are satisfied by duplicating an handle, and private shares are
                // child VMOs.
                //
                // Minfs and blobfs may require customization.  In particular, they may want to
                // track not just number of connections to a file, but also the number of
                // outstanding child VMOs.  While it is possible with the `init_vmo`/`consume_vmo`
                // model currently implemented, it is very likely that adding another customization
                // callback here will make the implementation of those files systems easier.
                let vmo_rights = vmo_flags_to_rights(flags);
                // Unless private sharing mode is specified, we always default to shared.
                let result = if flags & VMO_FLAG_PRIVATE != 0 {
                    Self::get_as_private(&vmo, vmo_rights, *size)
                }
                else {
                    Self::get_as_shared(&vmo, vmo_rights)
                };
                // Ideally a match statement would be more readable here, but using one here causes
                // within the update_initialized_state! macro causes a syntax error ("unexpected
                // token in input"), so for now we just use an if statement.
                if let Ok(vmo) = result {
                    (zx::Status::OK, Some(Buffer{vmo, size: *size}))
                } else {
                    (result.unwrap_err(), None)
                }
            }
        };

        responder(status, buffer)
    }

    fn get_as_shared(vmo: &zx::Vmo, mut rights: zx::Rights) -> Result<zx::Vmo, zx::Status> {
        // Add set of basic rights to include in shared mode before duplicating the VMO handle.
        rights |= zx::Rights::BASIC | zx::Rights::MAP | zx::Rights::GET_PROPERTY;
        vmo.as_handle_ref().duplicate(rights).map(Into::into)
    }

    fn get_as_private(
        vmo: &zx::Vmo,
        mut rights: zx::Rights,
        size: u64,
    ) -> Result<zx::Vmo, zx::Status> {
        // Add set of basic rights to include in private mode, ensuring we provide SET_PROPERTY.
        rights |= zx::Rights::BASIC
            | zx::Rights::MAP
            | zx::Rights::GET_PROPERTY
            | zx::Rights::SET_PROPERTY;

        // Ensure we give out a copy-on-write clone.
        let mut child_options = zx::VmoChildOptions::SNAPSHOT_AT_LEAST_ON_WRITE;
        // If we don't need a writable clone, we need to add CHILD_NO_WRITE since
        // SNAPSHOT_AT_LEAST_ON_WRITE removes ZX_RIGHT_EXECUTE even if the parent VMO has it, but
        // adding CHILD_NO_WRITE will ensure EXECUTE is maintained.
        if !rights.contains(zx::Rights::WRITE) {
            child_options |= zx::VmoChildOptions::NO_WRITE;
        } else {
            // If we need a writable clone, ensure it can be resized.
            child_options |= zx::VmoChildOptions::RESIZABLE;
        }

        let new_vmo = vmo.create_child(child_options, 0, size)?;
        new_vmo.into_handle().replace_handle(rights).map(Into::into)
    }
}
