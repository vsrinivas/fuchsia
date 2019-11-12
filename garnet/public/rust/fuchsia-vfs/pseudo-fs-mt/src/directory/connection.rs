// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{inherit_rights_for_clone, send_on_open_with_error},
    directory::{
        common::{
            check_child_connection_flags, new_connection_validate_flags,
            POSIX_DIRECTORY_PROTECTION_ATTRIBUTES,
        },
        entry::DirectoryEntry,
        entry_container::{self, AsyncReadDirents},
        read_dirents,
    },
    execution_scope::ExecutionScope,
    path::Path,
};

use {
    failure::Error,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        DirectoryMarker, DirectoryObject, DirectoryRequest, DirectoryRequestStream, NodeAttributes,
        NodeInfo, NodeMarker, OutOfLineUnion, INO_UNKNOWN, MODE_TYPE_DIRECTORY, OPEN_FLAG_DESCRIBE,
    },
    fuchsia_async::Channel,
    fuchsia_zircon::{
        sys::{ZX_ERR_INVALID_ARGS, ZX_ERR_NOT_SUPPORTED, ZX_OK},
        Status,
    },
    futures::stream::StreamExt,
    std::{default::Default, iter, iter::ExactSizeIterator, mem::replace, sync::Arc},
};

pub trait ConnectionClient<TraversalPosition>:
    DirectoryEntry + entry_container::Observable<TraversalPosition> + Send + Sync
where
    TraversalPosition: Default + Send + Sync + 'static,
{
}

impl<TraversalPosition, T> ConnectionClient<TraversalPosition> for T
where
    TraversalPosition: Default + Send + Sync + 'static,
    T: DirectoryEntry + entry_container::Observable<TraversalPosition> + Send + Sync,
{
}

/// Represents a FIDL connection to a directory.  A single directory may contain multiple
/// connections.  An instances of the DirectoryConnection will also hold any state that is
/// "per-connection".  Currently that would be the access flags and the seek position.
pub struct DirectoryConnection<TraversalPosition>
where
    TraversalPosition: Default + Send + Sync + 'static,
{
    /// Execution scope this connection and any async operations and connections it creates will
    /// use.
    scope: ExecutionScope,

    directory: Arc<dyn ConnectionClient<TraversalPosition>>,

    requests: DirectoryRequestStream,

    /// Flags set on this connection when it was opened or cloned.
    flags: u32,

    /// Seek position for this connection to the directory.  We just store the element that was
    /// returned last by ReadDirents for this connection.  Next call will look for the next element
    /// in alphabetical order and resume from there.
    ///
    /// An alternative is to use an intrusive tree to have a dual index in both names and IDs that
    /// are assigned to the entries in insertion order.  Then we can store an ID instead of the
    /// full entry name.  This is what the C++ version is doing currently.
    ///
    /// It should be possible to do the same intrusive dual-indexing using, for example,
    ///
    ///     https://docs.rs/intrusive-collections/0.7.6/intrusive_collections/
    ///
    /// but, as, I think, at least for the pseudo directories, this approach is fine, and it simple
    /// enough.
    seek: TraversalPosition,
}

/// Return type for [`DirectoryConnection::handle_request()`].
enum ConnectionState {
    Alive,
    Closed,
}

impl<TraversalPosition> DirectoryConnection<TraversalPosition>
where
    TraversalPosition: Default + Send + Sync + 'static,
{
    /// Initializes a directory connection, checking the flags and sending `OnOpen` event if
    /// necessary.  Returns a [`DirectoryConnection`] object as a [`StreamFuture`], or in case of
    /// an error, sends an appropriate `OnOpen` event (if requested) and returns `None`.
    pub fn create_connection(
        scope: ExecutionScope,
        directory: Arc<dyn ConnectionClient<TraversalPosition>>,
        flags: u32,
        mode: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let flags = match new_connection_validate_flags(flags, mode) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        let (requests, control_handle) =
            match ServerEnd::<DirectoryMarker>::new(server_end.into_channel())
                .into_stream_and_control_handle()
            {
                Ok((requests, control_handle)) => (requests, control_handle),
                Err(_) => {
                    // As we report all errors on `server_end`, if we failed to send an error over
                    // this connection, there is nowhere to send the error tothe error to.
                    return;
                }
            };

        if flags & OPEN_FLAG_DESCRIBE != 0 {
            let mut info = NodeInfo::Directory(DirectoryObject);
            match control_handle
                .send_on_open_(Status::OK.into_raw(), Some(OutOfLineUnion(&mut info)))
            {
                Ok(()) => (),
                Err(_) => return,
            }
        }

        let handle_requests = DirectoryConnection::<TraversalPosition> {
            scope: scope.clone(),
            directory,
            requests,
            flags,
            seek: Default::default(),
        }
        .handle_requests();
        // If we failed to send the task to the executor, it is probably shut down or is in the
        // process of shutting down (this is the only error state currently).  So there is nothing
        // for us to do, but to ignore the request.  Connection will be closed when the connection
        // object is dropped.
        let _ = scope.spawn(Box::pin(handle_requests));
    }

    #[must_use = "handle_requests() returns an async task that needs to be run"]
    async fn handle_requests(mut self) {
        while let Some(request_or_err) = self.requests.next().await {
            match request_or_err {
                Err(_) => {
                    // FIDL level error, such as invalid message format and alike.  Close the
                    // connection on any unexpected error.
                    // TODO: Send an epitaph.
                    break;
                }
                Ok(request) => match self.handle_request(request).await {
                    Ok(ConnectionState::Alive) => (),
                    Ok(ConnectionState::Closed) => break,
                    Err(_) => {
                        // Protocol level error.  Close the connection on any unexpected error.
                        // TODO: Send an epitaph.
                        break;
                    }
                },
            }
        }
    }

    /// Handle a [`DirectoryRequest`].  This function is responsible for handing all the basic
    /// direcotry operations.
    // TODO(fxb/37419): Remove default handling after methods landed.
    #[allow(unreachable_patterns)]
    async fn handle_request(&mut self, req: DirectoryRequest) -> Result<ConnectionState, Error> {
        match req {
            DirectoryRequest::Clone { flags, object, control_handle: _ } => {
                self.handle_clone(flags, 0, object);
            }
            DirectoryRequest::Close { responder } => {
                responder.send(ZX_OK)?;
                return Ok(ConnectionState::Closed);
            }
            DirectoryRequest::Describe { responder } => {
                let mut info = NodeInfo::Directory(DirectoryObject);
                responder.send(&mut info)?;
            }
            DirectoryRequest::Sync { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DirectoryRequest::GetAttr { responder } => {
                let mut attrs = NodeAttributes {
                    mode: MODE_TYPE_DIRECTORY | POSIX_DIRECTORY_PROTECTION_ATTRIBUTES,
                    id: INO_UNKNOWN,
                    content_size: 0,
                    storage_size: 0,
                    link_count: 1,
                    creation_time: 0,
                    modification_time: 0,
                };
                responder.send(ZX_OK, &mut attrs)?;
            }
            DirectoryRequest::SetAttr { flags: _, attributes: _, responder } => {
                // According to zircon/system/fidl/fuchsia-io/io.fidl the only flag that might be
                // modified through this call is OPEN_FLAG_APPEND, and it is not supported by a
                // Simple directory.
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DirectoryRequest::Open { flags, mode, path, object, control_handle: _ } => {
                self.handle_open(flags, mode, path, object);
            }
            DirectoryRequest::Unlink { path: _, responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DirectoryRequest::ReadDirents { max_bytes, responder } => {
                self.handle_read_dirents(max_bytes, |status, entries| {
                    responder.send(status.into_raw(), entries)
                })
                .await?;
            }
            DirectoryRequest::Rewind { responder } => {
                self.seek = Default::default();
                responder.send(ZX_OK)?;
            }
            DirectoryRequest::GetToken { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED, None)?;
            }
            DirectoryRequest::Rename { src: _, dst_parent_token: _, dst: _, responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DirectoryRequest::Link { src: _, dst_parent_token: _, dst: _, responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DirectoryRequest::Watch { mask, options, watcher, responder } => {
                if options != 0 {
                    responder.send(ZX_ERR_INVALID_ARGS)?;
                } else {
                    let channel = Channel::from_channel(watcher)?;
                    self.handle_watch(mask, channel, |status| responder.send(status.into_raw()))
                        .await?;
                }
            }
            _ => {}
        }
        Ok(ConnectionState::Alive)
    }

    fn handle_clone(&self, flags: u32, mode: u32, server_end: ServerEnd<NodeMarker>) {
        let flags = match inherit_rights_for_clone(self.flags, flags) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        Self::create_connection(
            self.scope.clone(),
            self.directory.clone(),
            flags,
            mode,
            server_end,
        );
    }

    fn handle_open(
        &self,
        flags: u32,
        mut mode: u32,
        path: String,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if path == "/" || path == "" {
            send_on_open_with_error(flags, server_end, Status::BAD_PATH);
            return;
        }

        if path == "." || path == "./" {
            self.handle_clone(flags, mode, server_end);
            return;
        }

        let path = match Path::validate_and_split(path) {
            Ok(path) => path,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        if path.is_dir() {
            mode |= MODE_TYPE_DIRECTORY;
        }

        let flags = match check_child_connection_flags(self.flags, flags) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        // It is up to the open method to handle OPEN_FLAG_DESCRIBE from this point on.
        let directory = self.directory.clone();
        directory.open(self.scope.clone(), flags, mode, path, server_end);
    }

    async fn handle_read_dirents<R>(
        &mut self,
        max_bytes: u64,
        responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, &mut dyn ExactSizeIterator<Item = u8>) -> Result<(), fidl::Error>,
    {
        let res = {
            let directory = self.directory.clone();
            match directory.read_dirents(
                replace(&mut self.seek, Default::default()),
                read_dirents::Sink::<TraversalPosition>::new(max_bytes),
            ) {
                AsyncReadDirents::Immediate(res) => res,
                AsyncReadDirents::Future(fut) => fut.await,
            }
        };

        let done_or_err = match res {
            Ok(sealed) => sealed.open().downcast::<read_dirents::Done<TraversalPosition>>(),
            Err(status) => return responder(status, &mut iter::empty()),
        };

        match done_or_err {
            Ok(done) => {
                self.seek = done.pos;
                responder(done.status, &mut done.buf.into_iter())
            }
            Err(_) => {
                debug_assert!(
                    false,
                    "`read_dirents()` returned a `dirents_sink::Sealed` instance that is not \
                     an instance of the `read_dirents::Done`.  This is a bug in the \
                     `read_dirents()` implementation."
                );
                responder(Status::NOT_SUPPORTED, &mut iter::empty())
            }
        }
    }

    async fn handle_watch<R>(
        &mut self,
        mask: u32,
        channel: Channel,
        responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        let directory = self.directory.clone();
        let status = directory.register_watcher(self.scope.clone(), mask, channel);
        responder(status)
    }
}
