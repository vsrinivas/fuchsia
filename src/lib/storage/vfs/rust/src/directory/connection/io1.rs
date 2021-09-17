// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{inherit_rights_for_clone, send_on_open_with_error, IntoAny, GET_FLAGS_VISIBLE},
    directory::{
        common::check_child_connection_flags, connection::util::OpenDirectory,
        entry::DirectoryEntry, entry_container::Directory, read_dirents,
        traversal_position::TraversalPosition,
    },
    execution_scope::ExecutionScope,
    path::Path,
};

use {
    anyhow::Error,
    fidl::{endpoints::ServerEnd, Handle},
    fidl_fuchsia_io::{
        DirectoryObject, NodeAttributes, NodeInfo, NodeMarker, INO_UNKNOWN, OPEN_FLAG_CREATE,
        OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DIRECTORY, OPEN_FLAG_NODE_REFERENCE,
        OPEN_FLAG_NOT_DIRECTORY, OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_io_admin::{DirectoryAdminRequest, DirectoryAdminRequestStream},
    fuchsia_async::Channel,
    fuchsia_zircon::{
        sys::{ZX_ERR_INVALID_ARGS, ZX_ERR_NOT_SUPPORTED, ZX_OK},
        Status,
    },
    futures::{future::BoxFuture, stream::StreamExt},
    std::{default::Default, sync::Arc},
};

/// Return type for `BaseConnection::handle_request` and [`DerivedConnection::handle_request`].
pub enum ConnectionState {
    /// Connection is still alive.
    Alive,
    /// Connection have received Node::Close message and should be closed.
    Closed,
}

/// This is an API a derived directory connection needs to implement, in order for the
/// `BaseConnection` to be able to interact with it.
pub trait DerivedConnection: Send + Sync {
    type Directory: BaseConnectionClient + ?Sized;

    fn new(scope: ExecutionScope, directory: OpenDirectory<Self::Directory>, flags: u32) -> Self;

    /// Initializes a directory connection, checking the flags and sending `OnOpen` event if
    /// necessary.  Then either runs this connection inside of the specified `scope` or, in case of
    /// an error, sends an appropriate `OnOpen` event (if requested) over the `server_end`
    /// connection.
    /// If an error occurs, create_connection() must call close() on the directory.
    fn create_connection(
        scope: ExecutionScope,
        directory: OpenDirectory<Self::Directory>,
        flags: u32,
        server_end: ServerEnd<NodeMarker>,
    );

    fn entry_not_found(
        scope: ExecutionScope,
        parent: Arc<dyn DirectoryEntry>,
        flags: u32,
        mode: u32,
        name: &str,
        path: &Path,
    ) -> Result<Arc<dyn DirectoryEntry>, Status>;

    fn handle_request(
        &mut self,
        request: DirectoryAdminRequest,
    ) -> BoxFuture<'_, Result<ConnectionState, Error>>;
}

/// This is an API a directory needs to implement, in order for the `BaseConnection` to be able to
/// interact with it.
pub trait BaseConnectionClient: DirectoryEntry + Directory + Send + Sync {}

impl<T> BaseConnectionClient for T where T: DirectoryEntry + Directory + Send + Sync + 'static {}

/// Handles functionality shared between mutable and immutable FIDL connections to a directory.  A
/// single directory may contain multiple connections.  Instances of the `BaseConnection`
/// will also hold any state that is "per-connection".  Currently that would be the access flags
/// and the seek position.
pub(in crate::directory) struct BaseConnection<Connection>
where
    Connection: DerivedConnection + 'static,
{
    /// Execution scope this connection and any async operations and connections it creates will
    /// use.
    pub(in crate::directory) scope: ExecutionScope,

    pub(in crate::directory) directory: OpenDirectory<Connection::Directory>,

    /// Flags set on this connection when it was opened or cloned.
    pub(in crate::directory) flags: u32,

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

#[must_use = "handle_requests() returns an async task that needs to be run"]
pub(in crate::directory) async fn handle_requests<Connection>(
    mut requests: DirectoryAdminRequestStream,
    mut connection: Connection,
) where
    Connection: DerivedConnection,
{
    while let Some(request_or_err) = requests.next().await {
        match request_or_err {
            Err(_) => {
                // FIDL level error, such as invalid message format and alike.  Close the
                // connection on any unexpected error.
                // TODO: Send an epitaph.
                break;
            }
            Ok(request) => match connection.handle_request(request).await {
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
    // The underlying directory will be closed automatically when the OpenDirectory is dropped.
}

impl<Connection> BaseConnection<Connection>
where
    Connection: DerivedConnection,
{
    /// Constructs an instance of `BaseConnection` - to be used by derived connections, when they
    /// need to create a nested `BaseConnection` "sub-object".  But when implementing
    /// `create_connection`, derived connections should use the [`create_connection`] call.
    pub(in crate::directory) fn new(
        scope: ExecutionScope,
        directory: OpenDirectory<Connection::Directory>,
        flags: u32,
    ) -> Self {
        BaseConnection { scope, directory, flags, seek: Default::default() }
    }

    /// Handle a [`DirectoryRequest`].  This function is responsible for handing all the basic
    /// directory operations.
    // TODO(fxbug.dev/37419): Remove default handling after methods landed.
    #[allow(unreachable_patterns)]
    pub(in crate::directory) async fn handle_request(
        &mut self,
        request: DirectoryAdminRequest,
    ) -> Result<ConnectionState, Error> {
        match request {
            DirectoryAdminRequest::Clone { flags, object, control_handle: _ } => {
                fuchsia_trace::duration!("storage", "Directory::Clone");
                self.handle_clone(flags, 0, object);
            }
            DirectoryAdminRequest::Close { responder } => {
                fuchsia_trace::duration!("storage", "Directory::Close");
                let status = match self.directory.close() {
                    Ok(()) => Status::OK,
                    Err(e) => e,
                };
                responder.send(status.into_raw())?;
                return Ok(ConnectionState::Closed);
            }
            DirectoryAdminRequest::Close2 { responder } => {
                fuchsia_trace::duration!("storage", "Directory::Close2");
                responder.send(&mut self.directory.close().map_err(|status| status.into_raw()))?;
                return Ok(ConnectionState::Closed);
            }
            DirectoryAdminRequest::Describe { responder } => {
                fuchsia_trace::duration!("storage", "Directory::Describe");
                let mut info = NodeInfo::Directory(DirectoryObject);
                responder.send(&mut info)?;
            }
            DirectoryAdminRequest::GetAttr { responder } => {
                fuchsia_trace::duration!("storage", "Directory::GetAttr");
                let (mut attrs, status) = match self.directory.get_attrs().await {
                    Ok(attrs) => (attrs, ZX_OK),
                    Err(status) => (
                        NodeAttributes {
                            mode: 0,
                            id: INO_UNKNOWN,
                            content_size: 0,
                            storage_size: 0,
                            link_count: 1,
                            creation_time: 0,
                            modification_time: 0,
                        },
                        status.into_raw(),
                    ),
                };
                responder.send(status, &mut attrs)?;
            }
            DirectoryAdminRequest::NodeGetFlags { responder } => {
                fuchsia_trace::duration!("storage", "Directory::GetFlags");
                responder.send(ZX_OK, self.flags & GET_FLAGS_VISIBLE)?;
            }
            DirectoryAdminRequest::NodeSetFlags { flags: _, responder } => {
                fuchsia_trace::duration!("storage", "Directory::SetFlags");
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DirectoryAdminRequest::Open { flags, mode, path, object, control_handle: _ } => {
                fuchsia_trace::duration!("storage", "Directory::Open");
                self.handle_open(flags, mode, path, object);
            }
            DirectoryAdminRequest::AddInotifyFilter { .. } => {
                fuchsia_trace::duration!("storage", "Directory::AddInotifyFilter");
            }
            DirectoryAdminRequest::AdvisoryLock { request: _, responder } => {
                fuchsia_trace::duration!("storage", "Directory::AdvisoryLock");
                responder.send(&mut Err(ZX_ERR_NOT_SUPPORTED))?;
            }
            DirectoryAdminRequest::ReadDirents { max_bytes, responder } => {
                fuchsia_trace::duration!("storage", "Directory::ReadDirents");
                self.handle_read_dirents(max_bytes, |status, entries| {
                    responder.send(status.into_raw(), entries)
                })
                .await?;
            }
            DirectoryAdminRequest::Rewind { responder } => {
                fuchsia_trace::duration!("storage", "Directory::Rewind");
                self.seek = Default::default();
                responder.send(ZX_OK)?;
            }
            DirectoryAdminRequest::Link { src, dst_parent_token, dst, responder } => {
                fuchsia_trace::duration!("storage", "Directory::Link");
                self.handle_link(&src, dst_parent_token, dst, |status| {
                    responder.send(status.into_raw())
                })
                .await?;
            }
            DirectoryAdminRequest::Watch { mask, options, watcher, responder } => {
                fuchsia_trace::duration!("storage", "Directory::Watch");
                if options != 0 {
                    responder.send(ZX_ERR_INVALID_ARGS)?;
                } else {
                    let channel = Channel::from_channel(watcher)?;
                    self.handle_watch(mask, channel, |status| responder.send(status.into_raw()))?;
                }
            }
            DirectoryAdminRequest::QueryFilesystem { responder } => {
                fuchsia_trace::duration!("storage", "Directory::QueryFilesystem");
                match self.directory.query_filesystem() {
                    Err(status) => responder.send(status.into_raw(), None)?,
                    Ok(mut info) => responder.send(0, Some(&mut info))?,
                }
            }
            DirectoryAdminRequest::Mount { responder, .. } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?
            }
            DirectoryAdminRequest::MountAndCreate { responder, .. } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?
            }
            DirectoryAdminRequest::Unmount { responder } => responder.send(ZX_ERR_NOT_SUPPORTED)?,
            DirectoryAdminRequest::UnmountNode { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED, None)?
            }
            DirectoryAdminRequest::GetDevicePath { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED, None)?
            }
            DirectoryAdminRequest::Unlink { responder, .. } => {
                responder.send(&mut Err(ZX_ERR_NOT_SUPPORTED))?;
            }
            DirectoryAdminRequest::GetToken { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED, None)?;
            }
            DirectoryAdminRequest::Rename { src: _, dst_parent_token: _, dst: _, responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DirectoryAdminRequest::Rename2 { src: _, dst_parent_token: _, dst: _, responder } => {
                responder.send(&mut Err(ZX_ERR_NOT_SUPPORTED))?;
            }
            DirectoryAdminRequest::SetAttr { responder, .. } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            DirectoryAdminRequest::Sync { responder } => {
                responder.send(ZX_ERR_NOT_SUPPORTED)?;
            }
            _ => {} // TODO(https://fxbug.dev/77623): Remove when the transition is complete.
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

        self.directory.clone().open(self.scope.clone(), flags, mode, Path::dot(), server_end);
    }

    fn handle_open(
        &self,
        mut flags: u32,
        mode: u32,
        path: String,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if self.flags & OPEN_FLAG_NODE_REFERENCE != 0 {
            send_on_open_with_error(flags, server_end, Status::BAD_HANDLE);
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
            flags |= OPEN_FLAG_DIRECTORY;
        }

        let (flags, mode) = match check_child_connection_flags(self.flags, flags, mode) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        // Note that we reject both OPEN_FLAG_CREATE and OPEN_FLAG_CREATE_IF_ABSENT, rather
        // than just OPEN_FLAG_CREATE_IF_ABSENT. This matches the behaviour of the C++
        // filesystems.
        if path.is_dot()
            && flags & (OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT | OPEN_FLAG_NOT_DIRECTORY)
                != 0
        {
            send_on_open_with_error(flags, server_end, Status::INVALID_ARGS);
            return;
        }

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
        R: FnOnce(Status, &[u8]) -> Result<(), fidl::Error>,
    {
        if self.flags & OPEN_FLAG_NODE_REFERENCE != 0 {
            return responder(Status::BAD_HANDLE, &[]);
        }

        let done_or_err =
            match self.directory.read_dirents(&self.seek, read_dirents::Sink::new(max_bytes)).await
            {
                Ok((new_pos, sealed)) => {
                    self.seek = new_pos;
                    sealed.open().downcast::<read_dirents::Done>()
                }
                Err(status) => return responder(status, &[]),
            };

        match done_or_err {
            Ok(done) => responder(done.status, &done.buf),
            Err(_) => {
                debug_assert!(
                    false,
                    "`read_dirents()` returned a `dirents_sink::Sealed` instance that is not \
                     an instance of the `read_dirents::Done`.  This is a bug in the \
                     `read_dirents()` implementation."
                );
                responder(Status::NOT_SUPPORTED, &[])
            }
        }
    }

    async fn handle_link<R>(
        &self,
        source_name: &str,
        target_parent_token: Handle,
        target_name: String,
        responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        if source_name.contains('/') || target_name.contains('/') {
            return responder(Status::INVALID_ARGS);
        }

        let token_registry = match self.scope.token_registry() {
            None => return responder(Status::NOT_SUPPORTED),
            Some(registry) => registry,
        };

        if self.flags & OPEN_RIGHT_WRITABLE == 0 {
            return responder(Status::BAD_HANDLE);
        }

        let target_parent = match token_registry.get_container(target_parent_token) {
            Err(status) => return responder(status),
            Ok(None) => return responder(Status::NOT_FOUND),
            Ok(Some(entry)) => entry,
        };

        match target_parent.link(target_name, self.directory.clone().into_any(), source_name).await
        {
            Ok(()) => responder(Status::OK),
            Err(status) => responder(status),
        }
    }

    fn handle_watch<R>(
        &mut self,
        mask: u32,
        channel: Channel,
        responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        let directory = self.directory.clone();
        responder(
            directory
                .register_watcher(self.scope.clone(), mask, channel)
                .err()
                .unwrap_or(Status::OK),
        )
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::directory::immutable::simple::simple,
        fidl_fuchsia_io::{
            DirectoryMarker, NodeEvent, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, OPEN_FLAG_DESCRIBE,
            OPEN_FLAG_DIRECTORY, OPEN_RIGHT_READABLE,
        },
        fuchsia_async as fasync, fuchsia_zircon as zx,
        futures::prelude::*,
        matches::assert_matches,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_open_not_found() {
        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        let dir = simple();
        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE,
            MODE_TYPE_DIRECTORY,
            Path::dot(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let (node_proxy, node_server_end) =
            fidl::endpoints::create_proxy().expect("Create proxy to succeed");

        // Try to open a file that doesn't exist.
        assert_matches!(
            dir_proxy.open(OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "foo", node_server_end),
            Ok(())
        );

        // The channel also be closed with a NOT_FOUND epitaph.
        assert_matches!(
            node_proxy.describe().await,
            Err(fidl::Error::ClientChannelClosed {
                status: zx::Status::NOT_FOUND,
                protocol_name: "(anonymous) Node",
            })
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_not_found_event_stream() {
        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        let dir = simple();
        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE,
            MODE_TYPE_DIRECTORY,
            Path::dot(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let (node_proxy, node_server_end) =
            fidl::endpoints::create_proxy().expect("Create proxy to succeed");

        // Try to open a file that doesn't exist.
        assert_matches!(
            dir_proxy.open(OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "foo", node_server_end),
            Ok(())
        );

        // The event stream should be closed with the epitaph.
        let mut event_stream = node_proxy.take_event_stream();
        assert_matches!(
            event_stream.try_next().await,
            Err(fidl::Error::ClientChannelClosed {
                status: zx::Status::NOT_FOUND,
                protocol_name: "(anonymous) Node",
            })
        );
        assert_matches!(event_stream.try_next().await, Ok(None));
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_with_describe_not_found() {
        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        let dir = simple();
        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE,
            MODE_TYPE_DIRECTORY,
            Path::dot(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let (node_proxy, node_server_end) =
            fidl::endpoints::create_proxy().expect("Create proxy to succeed");

        // Try to open a file that doesn't exist.
        assert_matches!(
            dir_proxy.open(
                OPEN_FLAG_DESCRIBE | OPEN_RIGHT_READABLE,
                MODE_TYPE_FILE,
                "foo",
                node_server_end,
            ),
            Ok(())
        );

        // The channel should be closed with a NOT_FOUND epitaph.
        assert_matches!(
            node_proxy.describe().await,
            Err(fidl::Error::ClientChannelClosed {
                status: zx::Status::NOT_FOUND,
                protocol_name: "(anonymous) Node",
            })
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_describe_not_found_event_stream() {
        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        let dir = simple();
        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE,
            MODE_TYPE_DIRECTORY,
            Path::dot(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let (node_proxy, node_server_end) =
            fidl::endpoints::create_proxy().expect("Create proxy to succeed");

        // Try to open a file that doesn't exist.
        assert_matches!(
            dir_proxy.open(
                OPEN_FLAG_DESCRIBE | OPEN_RIGHT_READABLE,
                MODE_TYPE_FILE,
                "foo",
                node_server_end,
            ),
            Ok(())
        );

        // The event stream should return that the file does not exist.
        let mut event_stream = node_proxy.take_event_stream();
        assert_matches!(
            event_stream.try_next().await,
            Ok(Some(NodeEvent::OnOpen_ {
                s,
                info: None,
            }))
            if Status::from_raw(s) == Status::NOT_FOUND
        );
        assert_matches!(
            event_stream.try_next().await,
            Err(fidl::Error::ClientChannelClosed {
                status: zx::Status::NOT_FOUND,
                protocol_name: "(anonymous) Node",
            })
        );
        assert_matches!(event_stream.try_next().await, Ok(None));
    }
}
