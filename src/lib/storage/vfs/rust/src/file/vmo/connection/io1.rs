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
    file::common::{
        get_backing_memory_validate_flags, new_connection_validate_flags, vmo_flags_to_rights,
    },
    file::vmo::{asynchronous::VmoFileState, connection::VmoFileInterface},
};

use {
    anyhow::Error,
    fidl::{encoding::Decodable, endpoints::ServerEnd, prelude::*},
    fidl_fuchsia_io as fio,
    fidl_fuchsia_mem::Buffer,
    fuchsia_zircon::{
        self as zx,
        sys::{ZX_ERR_NOT_SUPPORTED, ZX_OK},
        AsHandleRef, HandleBased,
    },
    futures::{lock::MutexGuard, stream::StreamExt},
    static_assertions::assert_eq_size,
    std::{convert::TryInto, sync::Arc},
};

/// Represents a FIDL connection to a file.
pub struct VmoFileConnection {
    /// Execution scope this connection and any async operations and connections it creates will
    /// use.
    scope: ExecutionScope,

    /// File this connection is associated with.
    file: Arc<dyn VmoFileInterface>,

    /// Wraps a FIDL connection, providing messages coming from the client.
    requests: fio::FileRequestStream,

    /// Either the "flags" value passed into [`DirectoryEntry::open()`], or the "flags" value
    /// received with [`FileRequest::Clone()`].
    flags: fio::OpenFlags,

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
        flags: fio::OpenFlags,
        server_end: ServerEnd<fio::NodeMarker>,
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
        flags: fio::OpenFlags,
        server_end: ServerEnd<fio::NodeMarker>,
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

            if flags.intersects(fio::OpenFlags::TRUNCATE) {
                if let Err(status) = Self::truncate_vmo(&mut *state, 0) {
                    send_on_open_with_error(flags, server_end, status);
                    return;
                }
            }

            match &mut *state {
                None => {
                    debug_assert!(false, "`ensure_vmo` did not initialize the state.");
                    send_on_open_with_error(flags, server_end, zx::Status::INTERNAL);
                    return;
                }
                Some(VmoFileState { connection_count, .. }) => {
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

        let connection = VmoFileConnection { scope, file, requests, flags, seek: 0 };

        if flags.intersects(fio::OpenFlags::DESCRIBE) {
            match control_handle
                .send_on_open_(zx::Status::OK.into_raw(), Some(&mut connection.node_info()))
            {
                Ok(()) => {}
                Err(_) => {
                    // As we report all errors on `server_end`, if we failed to
                    // send an error over this connection, there is nowhere to
                    // send the error to.
                    return;
                }
            }
        }

        connection.handle_requests().await
    }

    async fn ensure_vmo<'state_guard>(
        file: Arc<dyn VmoFileInterface>,
        mut state: MutexGuard<'state_guard, Option<VmoFileState>>,
        server_end: ServerEnd<fio::NodeMarker>,
    ) -> Result<
        (MutexGuard<'state_guard, Option<VmoFileState>>, ServerEnd<fio::NodeMarker>),
        (zx::Status, ServerEnd<fio::NodeMarker>),
    > {
        if state.is_some() {
            return Ok((state, server_end));
        }

        let vmo = match file.init_vmo().await {
            Ok(vmo) => vmo,
            Err(status) => return Err((status, server_end)),
        };

        *state = Some(VmoFileState {
            vmo,
            // We are going to increment the connection count later, so it needs to
            // start at 0.
            connection_count: 0,
        });

        Ok((state, server_end))
    }

    fn truncate_vmo(state: &mut Option<VmoFileState>, new_size: u64) -> Result<(), zx::Status> {
        let state = state.as_mut().ok_or(zx::Status::INTERNAL)?;

        let capacity = state.vmo.get_size()?;

        if new_size > capacity {
            return Err(zx::Status::OUT_OF_RANGE);
        }

        assert_eq_size!(usize, u64);

        let old_size = state.vmo.get_content_size()?;

        if new_size < old_size {
            // Zero out old data (which will decommit).
            state.vmo.set_content_size(&new_size)?;
            state.vmo.op_range(zx::VmoOp::ZERO, new_size, old_size - new_size)?;
        } else if new_size > old_size {
            // Zero out the range we are extending into.
            state.vmo.op_range(zx::VmoOp::ZERO, old_size, new_size - old_size)?;
            state.vmo.set_content_size(&new_size)?;
        }

        Ok(())
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
        let _: Result<(), zx::Status> = self.handle_close().await;
    }

    fn node_info(&self) -> fio::NodeInfoDeprecated {
        if self.flags.intersects(fio::OpenFlags::NODE_REFERENCE) {
            fio::NodeInfoDeprecated::Service(fio::Service)
        } else {
            fio::NodeInfoDeprecated::File(fio::FileObject { event: None, stream: None })
        }
    }

    /// Handle a [`FileRequest`]. This function is responsible for handing all the file operations
    /// that operate on the connection-specific buffer.
    async fn handle_request(&mut self, req: fio::FileRequest) -> Result<ConnectionState, Error> {
        match req {
            fio::FileRequest::Clone { flags, object, control_handle: _ } => {
                self.handle_clone(self.flags, flags, object);
            }
            fio::FileRequest::Reopen { rights_request, object_request, control_handle: _ } => {
                let _ = object_request;
                todo!("https://fxbug.dev/77623: rights_request={:?}", rights_request);
            }
            fio::FileRequest::Close { responder } => {
                // We are going to close the connection anyways, so there is no way to handle this
                // error.
                let result = self.handle_close().await;
                // At this point we've decremented the connection count so we must make sure we
                // return ConnectionState::Closed here rather than an error since that can result in
                // us erroneously decrementing the connection count again.
                let _ = responder.send(&mut result.map_err(zx::Status::into_raw));
                return Ok(ConnectionState::Closed);
            }
            fio::FileRequest::DescribeDeprecated { responder } => {
                let () = responder.send(&mut self.node_info())?;
            }
            fio::FileRequest::Describe2 { responder } => {
                let () = responder.send(fio::FileInfo::EMPTY)?;
            }
            fio::FileRequest::GetConnectionInfo { responder } => {
                let _ = responder;
                todo!("https://fxbug.dev/77623");
            }
            fio::FileRequest::Sync { responder } => {
                // VMOs are always in sync.
                responder.send(&mut Ok(()))?;
            }
            fio::FileRequest::GetAttr { responder } => match self.handle_get_attr().await {
                Ok(mut attrs) => responder.send(0, &mut attrs)?,
                Err(status) => {
                    responder.send(status.into_raw(), &mut fio::NodeAttributes::new_empty())?
                }
            },
            fio::FileRequest::SetAttr { flags: _, attributes: _, responder } => {
                // According to https://fuchsia.googlesource.com/fuchsia/+/HEAD/sdk/fidl/fuchsia.io/
                // the only flag that might be modified through this call is OPEN_FLAG_APPEND, and
                // it is not supported at the moment.
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            fio::FileRequest::GetAttributes { query, responder } => {
                let _ = responder;
                todo!("https://fxbug.dev/77623: query={:?}", query);
            }
            fio::FileRequest::UpdateAttributes { payload, responder } => {
                let _ = responder;
                todo!("https://fxbug.dev/77623: payload={:?}", payload);
            }
            fio::FileRequest::Read { count, responder } => {
                let result = self.handle_read(count).await;
                responder.send(&mut result.map_err(zx::Status::into_raw))?;
            }
            fio::FileRequest::ReadAt { count, offset, responder } => {
                let result = self.handle_read_at(offset, count).await;
                responder.send(&mut result.map_err(zx::Status::into_raw))?;
            }
            fio::FileRequest::Write { data, responder } => {
                let result = self.handle_write(&data).await;
                responder.send(&mut result.map_err(zx::Status::into_raw))?;
            }
            fio::FileRequest::WriteAt { offset, data, responder } => {
                let result = self.handle_write_at(offset, &data).await;
                responder.send(&mut result.map_err(zx::Status::into_raw))?;
            }
            fio::FileRequest::Seek { origin, offset, responder } => {
                let result = self.handle_seek(offset, origin).await;
                responder.send(&mut result.map_err(zx::Status::into_raw))?;
            }
            fio::FileRequest::Resize { length, responder } => {
                let result = self.handle_truncate(length).await;
                responder.send(&mut result.map_err(zx::Status::into_raw))?;
            }
            fio::FileRequest::GetFlags { responder } => {
                responder.send(ZX_OK, self.flags & GET_FLAGS_VISIBLE)?;
            }
            fio::FileRequest::SetFlags { flags: _, responder } => {
                // TODO: Support OPEN_FLAG_APPEND?  It is the only flag that is allowed to be set
                // via this call according to fuchsia.io. It would be nice to have that explicitly
                // encoded in the API instead, I guess.
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            fio::FileRequest::GetBackingMemory { flags, responder } => {
                let result =
                    self.handle_get_backing_memory(flags).await.map(|Buffer { vmo, size: _ }| vmo);
                responder.send(&mut result.map_err(zx::Status::into_raw))?;
            }
            fio::FileRequest::AdvisoryLock { request: _, responder } => {
                responder.send(&mut Err(ZX_ERR_NOT_SUPPORTED))?;
            }
            fio::FileRequest::Query { responder } => {
                responder.send(
                    if self.flags.intersects(fio::OpenFlags::NODE_REFERENCE) {
                        fio::NODE_PROTOCOL_NAME
                    } else {
                        fio::FILE_PROTOCOL_NAME
                    }
                    .as_bytes(),
                )?;
            }
            fio::FileRequest::QueryFilesystem { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED, None)?;
            }
        }
        Ok(ConnectionState::Alive)
    }

    fn handle_clone(
        &mut self,
        parent_flags: fio::OpenFlags,
        current_flags: fio::OpenFlags,
        server_end: ServerEnd<fio::NodeMarker>,
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

    async fn handle_close(&mut self) -> Result<(), zx::Status> {
        let state = &mut *self.file.state().await;
        let state = state.as_mut().ok_or(zx::Status::INTERNAL)?;
        state.connection_count -= 1;
        Ok(())
    }

    async fn handle_get_attr(&mut self) -> Result<fio::NodeAttributes, zx::Status> {
        let state = self.file.state().await;
        let state = state.as_ref().ok_or(zx::Status::INTERNAL)?;

        let content_size = state.vmo.get_content_size()?;

        Ok(fio::NodeAttributes {
            mode: fio::MODE_TYPE_FILE
                | rights_to_posix_mode_bits(
                    self.file.is_readable(),
                    self.file.is_writable(),
                    self.file.is_executable(),
                ),
            id: self.file.get_inode(),
            content_size,
            storage_size: content_size,
            link_count: 1,
            creation_time: 0,
            modification_time: 0,
        })
    }

    async fn handle_read(&mut self, count: u64) -> Result<Vec<u8>, zx::Status> {
        let bytes = self.handle_read_at(self.seek, count).await?;
        let count = bytes.len().try_into().unwrap();
        self.seek = self.seek.checked_add(count).unwrap();
        Ok(bytes)
    }

    async fn handle_read_at(&mut self, offset: u64, count: u64) -> Result<Vec<u8>, zx::Status> {
        if !self.flags.intersects(fio::OpenFlags::RIGHT_READABLE) {
            return Err(zx::Status::BAD_HANDLE);
        }
        if count > fio::MAX_TRANSFER_SIZE {
            return Err(zx::Status::OUT_OF_RANGE);
        }

        let state = self.file.state().await;
        let state = state.as_ref().ok_or(zx::Status::INTERNAL)?;

        let size = state.vmo.get_content_size()?;

        match size.checked_sub(offset) {
            None => Ok(Vec::new()),
            Some(rem) => {
                let count = core::cmp::min(count, rem);

                assert_eq_size!(usize, u64);
                let count = count.try_into().unwrap();

                let mut buffer = vec![0; count];
                state.vmo.read(&mut buffer, offset)?;
                Ok(buffer)
            }
        }
    }

    async fn handle_write(&mut self, content: &[u8]) -> Result<u64, zx::Status> {
        let actual = self.handle_write_at(self.seek, content).await?;
        self.seek += actual;
        Ok(actual)
    }

    async fn handle_write_at(
        &mut self,
        offset: u64,
        mut content: &[u8],
    ) -> Result<u64, zx::Status> {
        if !self.flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
            return Err(zx::Status::BAD_HANDLE);
        }
        if content.is_empty() {
            return Ok(0);
        }

        let mut state = self.file.state().await;
        let state = state.as_mut().ok_or(zx::Status::INTERNAL)?;

        let size = state.vmo.get_content_size()?;
        let capacity = state.vmo.get_size()?;

        if offset >= capacity {
            return Err(zx::Status::OUT_OF_RANGE);
        }
        let capacity = capacity - offset;
        assert_eq_size!(usize, u64);
        let capacity = capacity.try_into().unwrap();

        if content.len() > capacity {
            content = &content[..capacity];
        }

        let len = content.len().try_into().unwrap();

        if offset > size {
            state.vmo.op_range(zx::VmoOp::ZERO, size, offset - size)?;
        }
        state.vmo.write(content, offset)?;
        let end = offset + len;
        if end > size {
            state.vmo.set_content_size(&end)?;
        }
        Ok(len)
    }

    /// Move seek position to byte `offset` relative to the origin specified by `start.  Calls
    /// `responder` with an updated seek position, on success.
    async fn handle_seek(
        &mut self,
        offset: i64,
        origin: fio::SeekOrigin,
    ) -> Result<u64, zx::Status> {
        if self.flags.intersects(fio::OpenFlags::NODE_REFERENCE) {
            return Err(zx::Status::BAD_HANDLE);
        }

        let state = self.file.state().await;
        let state = state.as_ref().ok_or(zx::Status::INTERNAL)?;

        // There is an undocumented constraint that the seek offset can never exceed 63
        // bits. See https://fxbug.dev/100754.
        let origin: i64 = match origin {
            fio::SeekOrigin::Start => 0,
            fio::SeekOrigin::Current => self.seek,
            fio::SeekOrigin::End => state.vmo.get_content_size()?,
        }
        .try_into()
        .unwrap();
        match origin.checked_add(offset) {
            None => Err(zx::Status::OUT_OF_RANGE),
            Some(offset) => {
                let offset = offset
                    .try_into()
                    .map_err(|std::num::TryFromIntError { .. }| zx::Status::OUT_OF_RANGE)?;
                self.seek = offset;
                Ok(offset)
            }
        }
    }

    async fn handle_truncate(&mut self, length: u64) -> Result<(), zx::Status> {
        if !self.flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
            return Err(zx::Status::BAD_HANDLE);
        }

        Self::truncate_vmo(&mut *self.file.state().await, length)
    }

    async fn handle_get_backing_memory(
        &mut self,
        flags: fio::VmoFlags,
    ) -> Result<Buffer, zx::Status> {
        let () = get_backing_memory_validate_flags(flags, self.flags)?;

        // The only sharing mode we support that disallows the VMO size to change currently
        // is PRIVATE_CLONE (`get_as_private`), so we require that to be set explicitly.
        if flags.contains(fio::VmoFlags::WRITE) && !flags.contains(fio::VmoFlags::PRIVATE_CLONE) {
            return Err(zx::Status::NOT_SUPPORTED);
        }

        // Disallow opening as both writable and executable. In addition to improving W^X
        // enforcement, this also eliminates any inconstiencies related to clones that use
        // SNAPSHOT_AT_LEAST_ON_WRITE since in that case, we cannot satisfy both requirements.
        if flags.contains(fio::VmoFlags::EXECUTE) && flags.contains(fio::VmoFlags::WRITE) {
            return Err(zx::Status::NOT_SUPPORTED);
        }

        let state = self.file.state().await;
        let state = state.as_ref().ok_or(zx::Status::INTERNAL)?;

        // Logic here matches fuchsia.io requirements and matches what works for memfs.
        // Shared requests are satisfied by duplicating an handle, and private shares are
        // child VMOs.
        //
        // Minfs and blobfs may require customization.  In particular, they may want to
        // track not just number of connections to a file, but also the number of
        // outstanding child VMOs.  While it is possible with the `init_vmo`
        // model currently implemented, it is very likely that adding another customization
        // callback here will make the implementation of those files systems easier.
        let vmo_rights = vmo_flags_to_rights(flags);
        // Unless private sharing mode is specified, we always default to shared.
        let size = state.vmo.get_content_size()?;
        let vmo = if flags.contains(fio::VmoFlags::PRIVATE_CLONE) {
            Self::get_as_private(&state.vmo, vmo_rights, size)
        } else {
            Self::get_as_shared(&state.vmo, vmo_rights)
        }?;
        Ok(Buffer { vmo, size })
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
