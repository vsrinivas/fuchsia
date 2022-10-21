// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    common::{inherit_rights_for_clone, send_on_open_with_error, IntoAny, GET_FLAGS_VISIBLE},
    directory::{
        common::check_child_connection_flags,
        connection::util::OpenDirectory,
        entry::DirectoryEntry,
        entry_container::{Directory, DirectoryWatcher},
        read_dirents,
        traversal_position::TraversalPosition,
    },
    execution_scope::ExecutionScope,
    path::Path,
};

use {
    anyhow::Error,
    fidl::{endpoints::ServerEnd, Handle},
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    futures::channel::oneshot,
    pin_project::pin_project,
    std::{
        convert::TryInto as _,
        default::Default,
        future::Future,
        pin::Pin,
        sync::Arc,
        task::{Context, Poll},
    },
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
    type Directory: Directory + ?Sized;

    /// Whether these connections support mutable connections.
    const MUTABLE: bool;

    fn new(
        scope: ExecutionScope,
        directory: OpenDirectory<Self::Directory>,
        flags: fio::OpenFlags,
    ) -> Self;

    /// Initializes a directory connection, checking the flags and sending `OnOpen` event if
    /// necessary.  Then either runs this connection inside of the specified `scope` or, in case of
    /// an error, sends an appropriate `OnOpen` event (if requested) over the `server_end`
    /// connection.
    /// If an error occurs, create_connection() must call close() on the directory.
    fn create_connection(
        scope: ExecutionScope,
        directory: Arc<Self::Directory>,
        flags: fio::OpenFlags,
        server_end: ServerEnd<fio::NodeMarker>,
    );

    fn entry_not_found(
        scope: ExecutionScope,
        parent: Arc<dyn DirectoryEntry>,
        flags: fio::OpenFlags,
        mode: u32,
        name: &str,
        path: &Path,
    ) -> Result<Arc<dyn DirectoryEntry>, zx::Status>;
}

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
    pub(in crate::directory) flags: fio::OpenFlags,

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

/// Takes a stream and a shutdown receiver and creates a new stream that will terminate when the
/// shutdown receiver is ready (and ignore any outstanding items in the original stream).
#[pin_project]
pub struct StreamWithShutdown<T: futures::Stream>(#[pin] T, #[pin] oneshot::Receiver<()>);

impl<T: futures::Stream> futures::Stream for StreamWithShutdown<T> {
    type Item = T::Item;

    fn poll_next(self: Pin<&mut Self>, cx: &mut Context) -> Poll<Option<Self::Item>> {
        let this = self.project();
        if let Poll::Ready(Ok(())) = this.1.poll(cx) {
            return Poll::Ready(None);
        }
        this.0.poll_next(cx)
    }
}

pub trait WithShutdown {
    fn with_shutdown(self, shutdown: oneshot::Receiver<()>) -> StreamWithShutdown<Self>
    where
        Self: futures::Stream + Sized,
    {
        StreamWithShutdown(self, shutdown)
    }
}

impl<T: futures::Stream> WithShutdown for T {}

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
        flags: fio::OpenFlags,
    ) -> Self {
        BaseConnection { scope, directory, flags, seek: Default::default() }
    }

    pub(in crate::directory) fn node_info(&self) -> fio::NodeInfoDeprecated {
        if self.flags.intersects(fio::OpenFlags::NODE_REFERENCE) {
            fio::NodeInfoDeprecated::Service(fio::Service)
        } else {
            fio::NodeInfoDeprecated::Directory(fio::DirectoryObject)
        }
    }

    /// Handle a [`DirectoryRequest`].  This function is responsible for handing all the basic
    /// directory operations.
    pub(in crate::directory) async fn handle_request(
        &mut self,
        request: fio::DirectoryRequest,
    ) -> Result<ConnectionState, Error> {
        match request {
            fio::DirectoryRequest::Clone { flags, object, control_handle: _ } => {
                fuchsia_trace::duration!("storage", "Directory::Clone");
                self.handle_clone(flags, 0, object);
            }
            fio::DirectoryRequest::Reopen { rights_request, object_request, control_handle: _ } => {
                fuchsia_trace::duration!("storage", "Directory::Reopen");
                let _ = object_request;
                todo!("https://fxbug.dev/77623: rights_request={:?}", rights_request);
            }
            fio::DirectoryRequest::Close { responder } => {
                fuchsia_trace::duration!("storage", "Directory::Close");
                responder.send(&mut self.directory.close().map_err(|status| status.into_raw()))?;
                return Ok(ConnectionState::Closed);
            }
            fio::DirectoryRequest::DescribeDeprecated { responder } => {
                fuchsia_trace::duration!("storage", "Directory::Describe");
                responder.send(&mut self.node_info())?;
            }
            fio::DirectoryRequest::GetConnectionInfo { responder } => {
                fuchsia_trace::duration!("storage", "Directory::GetConnectionInfo");
                let _ = responder;
                todo!("https://fxbug.dev/77623");
            }
            fio::DirectoryRequest::GetAttr { responder } => {
                fuchsia_trace::duration!("storage", "Directory::GetAttr");
                let (mut attrs, status) = match self.directory.get_attrs().await {
                    Ok(attrs) => (attrs, zx::Status::OK.into_raw()),
                    Err(status) => (
                        fio::NodeAttributes {
                            mode: 0,
                            id: fio::INO_UNKNOWN,
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
            fio::DirectoryRequest::GetAttributes { query, responder } => {
                fuchsia_trace::duration!("storage", "Directory::GetAttributes");
                let _ = responder;
                todo!("https://fxbug.dev/77623: query={:?}", query);
            }
            fio::DirectoryRequest::UpdateAttributes { payload: _, responder } => {
                fuchsia_trace::duration!("storage", "Directory::UpdateAttributes");
                responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))?;
            }
            fio::DirectoryRequest::GetFlags { responder } => {
                fuchsia_trace::duration!("storage", "Directory::GetFlags");
                responder.send(zx::Status::OK.into_raw(), self.flags & GET_FLAGS_VISIBLE)?;
            }
            fio::DirectoryRequest::SetFlags { flags: _, responder } => {
                fuchsia_trace::duration!("storage", "Directory::SetFlags");
                responder.send(zx::Status::NOT_SUPPORTED.into_raw())?;
            }
            fio::DirectoryRequest::Open { flags, mode, path, object, control_handle: _ } => {
                fuchsia_trace::duration!("storage", "Directory::Open");
                self.handle_open(flags, mode, path, object);
            }
            fio::DirectoryRequest::Open2 { path, protocols, object_request, control_handle: _ } => {
                fuchsia_trace::duration!("storage", "Directory::Open2");
                let _ = object_request;
                todo!("https://fxbug.dev/77623: path={} protocols={:?}", path, protocols);
            }
            fio::DirectoryRequest::AddInotifyFilter {
                path,
                filter,
                watch_descriptor,
                socket: _,
                responder: _,
            } => {
                fuchsia_trace::duration!("storage", "Directory::AddInotifyFilter");
                todo!(
                    "https://fxbug.dev/77623: path={} filter={:?} watch_descriptor={}",
                    path,
                    filter,
                    watch_descriptor
                );
            }
            fio::DirectoryRequest::AdvisoryLock { request: _, responder } => {
                fuchsia_trace::duration!("storage", "Directory::AdvisoryLock");
                responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))?;
            }
            fio::DirectoryRequest::ReadDirents { max_bytes, responder } => {
                fuchsia_trace::duration!("storage", "Directory::ReadDirents");
                let (status, entries) = self.handle_read_dirents(max_bytes).await;
                responder.send(status.into_raw(), entries.as_slice())?;
            }
            fio::DirectoryRequest::Enumerate { options, iterator, control_handle: _ } => {
                fuchsia_trace::duration!("storage", "Directory::Enumerate");
                let _ = iterator;
                todo!("https://fxbug.dev/77623: options={:?}", options);
            }
            fio::DirectoryRequest::Rewind { responder } => {
                fuchsia_trace::duration!("storage", "Directory::Rewind");
                self.seek = Default::default();
                responder.send(zx::Status::OK.into_raw())?;
            }
            fio::DirectoryRequest::Link { src, dst_parent_token, dst, responder } => {
                fuchsia_trace::duration!("storage", "Directory::Link");
                let status: zx::Status = self.handle_link(&src, dst_parent_token, dst).await.into();
                responder.send(status.into_raw())?;
            }
            fio::DirectoryRequest::Watch { mask, options, watcher, responder } => {
                fuchsia_trace::duration!("storage", "Directory::Watch");
                let status = if options != 0 {
                    zx::Status::INVALID_ARGS
                } else {
                    let watcher = watcher.try_into()?;
                    self.handle_watch(mask, watcher).into()
                };
                responder.send(status.into_raw())?;
            }
            fio::DirectoryRequest::Query { responder } => {
                let () = responder.send(
                    if self.flags.intersects(fio::OpenFlags::NODE_REFERENCE) {
                        fio::NODE_PROTOCOL_NAME
                    } else {
                        fio::DIRECTORY_PROTOCOL_NAME
                    }
                    .as_bytes(),
                )?;
            }
            fio::DirectoryRequest::QueryFilesystem { responder } => {
                fuchsia_trace::duration!("storage", "Directory::QueryFilesystem");
                match self.directory.query_filesystem() {
                    Err(status) => responder.send(status.into_raw(), None)?,
                    Ok(mut info) => responder.send(0, Some(&mut info))?,
                }
            }
            fio::DirectoryRequest::Unlink { name: _, options: _, responder } => {
                responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))?;
            }
            fio::DirectoryRequest::GetToken { responder } => {
                responder.send(zx::Status::NOT_SUPPORTED.into_raw(), None)?;
            }
            fio::DirectoryRequest::Rename { src: _, dst_parent_token: _, dst: _, responder } => {
                responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))?;
            }
            fio::DirectoryRequest::SetAttr { flags: _, attributes: _, responder } => {
                responder.send(zx::Status::NOT_SUPPORTED.into_raw())?;
            }
            fio::DirectoryRequest::Sync { responder } => {
                responder.send(&mut Err(zx::Status::NOT_SUPPORTED.into_raw()))?;
            }
        }
        Ok(ConnectionState::Alive)
    }

    fn handle_clone(
        &self,
        flags: fio::OpenFlags,
        mode: u32,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
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
        mut flags: fio::OpenFlags,
        mode: u32,
        path: String,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        if self.flags.intersects(fio::OpenFlags::NODE_REFERENCE) {
            send_on_open_with_error(flags, server_end, zx::Status::BAD_HANDLE);
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
            flags |= fio::OpenFlags::DIRECTORY;
        }

        let (flags, mode) = match check_child_connection_flags(self.flags, flags, mode) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };
        if path.is_dot() {
            if flags.intersects(fio::OpenFlags::NOT_DIRECTORY) {
                send_on_open_with_error(flags, server_end, zx::Status::INVALID_ARGS);
                return;
            }
            if flags.intersects(fio::OpenFlags::CREATE_IF_ABSENT) {
                send_on_open_with_error(flags, server_end, zx::Status::ALREADY_EXISTS);
                return;
            }
        }

        // It is up to the open method to handle OPEN_FLAG_DESCRIBE from this point on.
        let directory = self.directory.clone();
        directory.open(self.scope.clone(), flags, mode, path, server_end);
    }

    async fn handle_read_dirents(&mut self, max_bytes: u64) -> (zx::Status, Vec<u8>) {
        async {
            if self.flags.intersects(fio::OpenFlags::NODE_REFERENCE) {
                return Err(zx::Status::BAD_HANDLE);
            }

            let (new_pos, sealed) =
                self.directory.read_dirents(&self.seek, read_dirents::Sink::new(max_bytes)).await?;
            self.seek = new_pos;
            let read_dirents::Done { buf, status } = *sealed
                .open()
                .downcast::<read_dirents::Done>()
                .map_err(|_: Box<dyn std::any::Any>| {
                    #[cfg(debug)]
                    panic!(
                        "`read_dirents()` returned a `dirents_sink::Sealed`
                        instance that is not an instance of the \
                        `read_dirents::Done`. This is a bug in the \
                        `read_dirents()` implementation."
                    );
                    zx::Status::NOT_SUPPORTED
                })?;
            Ok((status, buf))
        }
        .await
        .unwrap_or_else(|status| (status, Vec::new()))
    }

    async fn handle_link(
        &self,
        source_name: &str,
        target_parent_token: Handle,
        target_name: String,
    ) -> Result<(), zx::Status> {
        if source_name.contains('/') || target_name.contains('/') {
            return Err(zx::Status::INVALID_ARGS);
        }

        if !self.flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
            return Err(zx::Status::BAD_HANDLE);
        }

        let (target_parent, _flags) = self
            .scope
            .token_registry()
            .get_owner(target_parent_token)?
            .ok_or(Err(zx::Status::NOT_FOUND))?;

        target_parent.link(target_name, self.directory.clone().into_any(), source_name).await
    }

    fn handle_watch(
        &mut self,
        mask: fio::WatchMask,
        watcher: DirectoryWatcher,
    ) -> Result<(), zx::Status> {
        let directory = self.directory.clone();
        directory.register_watcher(self.scope.clone(), mask, watcher)
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*, crate::directory::immutable::simple::simple, assert_matches::assert_matches,
        fidl_fuchsia_io as fio, fuchsia_async as fasync, fuchsia_zircon as zx, futures::prelude::*,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_open_not_found() {
        let (dir_proxy, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("Create proxy to succeed");

        let dir = simple();
        dir.open(
            ExecutionScope::new(),
            fio::OpenFlags::DIRECTORY | fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            Path::dot(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let (node_proxy, node_server_end) =
            fidl::endpoints::create_proxy().expect("Create proxy to succeed");

        // Try to open a file that doesn't exist.
        assert_matches!(
            dir_proxy.open(
                fio::OpenFlags::RIGHT_READABLE,
                fio::MODE_TYPE_FILE,
                "foo",
                node_server_end
            ),
            Ok(())
        );

        // The channel also be closed with a NOT_FOUND epitaph.
        assert_matches!(
            node_proxy.query().await,
            Err(fidl::Error::ClientChannelClosed {
                status: zx::Status::NOT_FOUND,
                protocol_name: "(anonymous) Node",
            })
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_not_found_event_stream() {
        let (dir_proxy, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("Create proxy to succeed");

        let dir = simple();
        dir.open(
            ExecutionScope::new(),
            fio::OpenFlags::DIRECTORY | fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            Path::dot(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let (node_proxy, node_server_end) =
            fidl::endpoints::create_proxy().expect("Create proxy to succeed");

        // Try to open a file that doesn't exist.
        assert_matches!(
            dir_proxy.open(
                fio::OpenFlags::RIGHT_READABLE,
                fio::MODE_TYPE_FILE,
                "foo",
                node_server_end
            ),
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
        let (dir_proxy, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("Create proxy to succeed");

        let dir = simple();
        dir.open(
            ExecutionScope::new(),
            fio::OpenFlags::DIRECTORY | fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            Path::dot(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let (node_proxy, node_server_end) =
            fidl::endpoints::create_proxy().expect("Create proxy to succeed");

        // Try to open a file that doesn't exist.
        assert_matches!(
            dir_proxy.open(
                fio::OpenFlags::DESCRIBE | fio::OpenFlags::RIGHT_READABLE,
                fio::MODE_TYPE_FILE,
                "foo",
                node_server_end,
            ),
            Ok(())
        );

        // The channel should be closed with a NOT_FOUND epitaph.
        assert_matches!(
            node_proxy.query().await,
            Err(fidl::Error::ClientChannelClosed {
                status: zx::Status::NOT_FOUND,
                protocol_name: "(anonymous) Node",
            })
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_describe_not_found_event_stream() {
        let (dir_proxy, dir_server_end) = fidl::endpoints::create_proxy::<fio::DirectoryMarker>()
            .expect("Create proxy to succeed");

        let dir = simple();
        dir.open(
            ExecutionScope::new(),
            fio::OpenFlags::DIRECTORY | fio::OpenFlags::RIGHT_READABLE,
            fio::MODE_TYPE_DIRECTORY,
            Path::dot(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let (node_proxy, node_server_end) =
            fidl::endpoints::create_proxy().expect("Create proxy to succeed");

        // Try to open a file that doesn't exist.
        assert_matches!(
            dir_proxy.open(
                fio::OpenFlags::DESCRIBE | fio::OpenFlags::RIGHT_READABLE,
                fio::MODE_TYPE_FILE,
                "foo",
                node_server_end,
            ),
            Ok(())
        );

        // The event stream should return that the file does not exist.
        let mut event_stream = node_proxy.take_event_stream();
        assert_matches!(
            event_stream.try_next().await,
            Ok(Some(fio::NodeEvent::OnOpen_ {
                s,
                info: None,
            }))
            if zx::Status::from_raw(s) == zx::Status::NOT_FOUND
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
