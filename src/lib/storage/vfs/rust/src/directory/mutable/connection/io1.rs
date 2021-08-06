// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Connection to a directory that can be modified by the client though a FIDL connection.

use crate::{
    common::send_on_open_with_error,
    directory::{
        common::new_connection_validate_flags,
        connection::{
            io1::{
                handle_requests, BaseConnection, BaseConnectionClient, ConnectionState,
                DerivedConnection,
            },
            util::OpenDirectory,
        },
        entry::DirectoryEntry,
        entry_container::MutableDirectory,
        mutable::entry_constructor::NewEntryType,
    },
    execution_scope::ExecutionScope,
    path::Path,
    registry::TokenRegistryClient,
};

use {
    anyhow::Error,
    either::{Either, Left, Right},
    fidl::{endpoints::ServerEnd, Handle},
    fidl_fuchsia_io::{
        DirectoryAdminMarker, DirectoryAdminRequest, DirectoryObject, NodeAttributes, NodeInfo,
        NodeMarker, OPEN_FLAG_CREATE, OPEN_FLAG_DESCRIBE, OPEN_RIGHT_WRITABLE,
    },
    fidl_fuchsia_io2::{UnlinkFlags, UnlinkOptions},
    fuchsia_zircon::Status,
    futures::future::BoxFuture,
    std::sync::Arc,
};

/// This is an API a mutable directory needs to implement, in order for a `MutableConnection` to be
/// able to interact with this it.
pub trait MutableConnectionClient: BaseConnectionClient + MutableDirectory {
    fn into_base_connection_client(self: Arc<Self>) -> Arc<dyn BaseConnectionClient>;

    fn into_mutable_directory(self: Arc<Self>) -> Arc<dyn MutableDirectory>;

    fn into_token_registry_client(self: Arc<Self>) -> Arc<dyn TokenRegistryClient>;
}

impl<T> MutableConnectionClient for T
where
    T: BaseConnectionClient + MutableDirectory + 'static,
{
    fn into_base_connection_client(self: Arc<Self>) -> Arc<dyn BaseConnectionClient> {
        self as Arc<dyn BaseConnectionClient>
    }

    fn into_mutable_directory(self: Arc<Self>) -> Arc<dyn MutableDirectory> {
        self as Arc<dyn MutableDirectory>
    }

    fn into_token_registry_client(self: Arc<Self>) -> Arc<dyn TokenRegistryClient> {
        self as Arc<dyn TokenRegistryClient>
    }
}

pub struct MutableConnection {
    base: BaseConnection<Self>,
}

impl DerivedConnection for MutableConnection {
    type Directory = dyn MutableConnectionClient;

    fn new(scope: ExecutionScope, directory: OpenDirectory<Self::Directory>, flags: u32) -> Self {
        MutableConnection { base: BaseConnection::<Self>::new(scope, directory, flags) }
    }

    fn create_connection(
        scope: ExecutionScope,
        directory: OpenDirectory<Self::Directory>,
        flags: u32,
        server_end: ServerEnd<NodeMarker>,
    ) {
        // TODO(fxbug.dev/82054): These flags should be validated before create_connection is called
        // since at this point the directory resource has already been opened/created.
        let flags = match new_connection_validate_flags(flags) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                return;
            }
        };

        let (requests, control_handle) =
            match ServerEnd::<DirectoryAdminMarker>::new(server_end.into_channel())
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
            match control_handle.send_on_open_(Status::OK.into_raw(), Some(&mut info)) {
                Ok(()) => (),
                Err(_) => return,
            }
        }

        let connection = Self::new(scope.clone(), directory, flags);

        let task = handle_requests::<Self>(requests, connection);
        // If we fail to send the task to the executor, it is probably shut down or is in the
        // process of shutting down (this is the only error state currently).  So there is nothing
        // for us to do - the connection will be closed automatically when the connection object is
        // dropped.
        let _ = scope.spawn(Box::pin(task));
    }

    fn entry_not_found(
        scope: ExecutionScope,
        parent: Arc<dyn DirectoryEntry>,
        flags: u32,
        mode: u32,
        name: &str,
        path: &Path,
    ) -> Result<Arc<dyn DirectoryEntry>, Status> {
        if flags & OPEN_FLAG_CREATE == 0 {
            return Err(Status::NOT_FOUND);
        }

        let type_ = NewEntryType::from_flags_and_mode(flags, mode, path.is_dir())?;

        let entry_constructor = match scope.entry_constructor() {
            None => return Err(Status::NOT_SUPPORTED),
            Some(constructor) => constructor,
        };

        entry_constructor.create_entry(parent, type_, name, path)
    }

    fn handle_request(
        &mut self,
        request: DirectoryAdminRequest,
    ) -> BoxFuture<'_, Result<ConnectionState, Error>> {
        Box::pin(async move {
            match self.handle_request(request).await {
                // If the request was *not* handled (i.e. we got the request back), we pass
                // the request back into the base handler and return that Result instead.
                Ok(Right(request)) => self.base.handle_request(request).await,
                // Otherwise, the request was handled, so we return the new state/error directly.
                Ok(Left(state)) => Ok(state),
                Err(error) => Err(error),
            }
        })
    }
}

impl MutableConnection {
    async fn handle_request(
        &mut self,
        request: DirectoryAdminRequest,
    ) -> Result<Either<ConnectionState, DirectoryAdminRequest>, Error> {
        match request {
            DirectoryAdminRequest::Unlink { path, responder } => {
                self.handle_unlink(path, None, |status| responder.send(status.into_raw())).await?;
            }
            DirectoryAdminRequest::Unlink2 { name, options, responder } => {
                self.handle_unlink(name, Some(options), |status| {
                    responder.send(&mut Result::from(status).map_err(|e| e.into_raw()))
                })
                .await?;
            }
            DirectoryAdminRequest::GetToken { responder } => {
                self.handle_get_token(|status, token| responder.send(status.into_raw(), token))?;
            }
            DirectoryAdminRequest::Rename { src, dst_parent_token, dst, responder } => {
                self.handle_rename(src, dst_parent_token, dst, |status| {
                    responder.send(status.into_raw())
                })
                .await?;
            }
            DirectoryAdminRequest::Rename2 { src, dst_parent_token, dst, responder } => {
                self.handle_rename(src, Handle::from(dst_parent_token), dst, |status| {
                    responder.send(&mut Result::from(status).map_err(|e| e.into_raw()))
                })
                .await?;
            }
            DirectoryAdminRequest::SetAttr { flags, attributes, responder } => {
                let status = match self.handle_setattr(flags, attributes).await {
                    Ok(()) => Status::OK,
                    Err(status) => status,
                };
                responder.send(status.into_raw())?;
            }
            DirectoryAdminRequest::Sync { responder } => {
                responder.send(
                    self.base.directory.sync().await.err().unwrap_or(Status::OK).into_raw(),
                )?;
            }
            _ => {
                // Since we haven't handled the request, we return the original request so that
                // it can be consumed by the base handler instead.
                return Ok(Right(request));
            }
        }
        Ok(Left(ConnectionState::Alive))
    }

    async fn handle_setattr(
        &mut self,
        flags: u32,
        attributes: NodeAttributes,
    ) -> Result<(), Status> {
        if self.base.flags & OPEN_RIGHT_WRITABLE == 0 {
            return Err(Status::BAD_HANDLE);
        }

        // TODO(jfsulliv): Consider always permitting attributes to be deferrable. The risk with
        // this is that filesystems would require a background flush of dirty attributes to disk.
        self.base.directory.set_attrs(flags, attributes).await
    }

    async fn handle_unlink<R>(
        &mut self,
        name: String,
        options: Option<UnlinkOptions>,
        responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        if self.base.flags & OPEN_RIGHT_WRITABLE == 0 {
            return responder(Status::BAD_HANDLE);
        }

        if name == "/" || name.is_empty() || name == "." || name == ".." || name == "./" {
            return responder(Status::INVALID_ARGS);
        }

        let path = match Path::validate_and_split(name) {
            Ok(path) => path,
            Err(status) => return responder(status),
        };

        if path.is_empty() {
            return responder(Status::INVALID_ARGS);
        }

        if !path.is_single_component() {
            return responder(Status::INVALID_ARGS);
        }

        let must_be_directory = match options {
            None => path.is_dir(),
            Some(options) => {
                if path.as_ref().contains('/') {
                    return responder(Status::INVALID_ARGS);
                }
                options.flags.map(|f| f.contains(UnlinkFlags::MustBeDirectory)).unwrap_or(false)
            }
        };

        match self.base.directory.clone().unlink(path.peek().unwrap(), must_be_directory).await {
            Ok(()) => responder(Status::OK),
            Err(status) => responder(status),
        }
    }

    fn handle_get_token<R>(&self, responder: R) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status, Option<Handle>) -> Result<(), fidl::Error>,
    {
        if self.base.flags & OPEN_RIGHT_WRITABLE == 0 {
            return responder(Status::BAD_HANDLE, None);
        }

        let token_registry = match self.base.scope.token_registry() {
            None => return responder(Status::NOT_SUPPORTED, None),
            Some(registry) => registry,
        };

        let token = {
            let dir_as_reg_client = self.base.directory.clone().into_token_registry_client();
            match token_registry.get_token(dir_as_reg_client) {
                Err(status) => return responder(status, None),
                Ok(token) => token,
            }
        };

        responder(Status::OK, Some(token))
    }

    async fn handle_rename<R>(
        &self,
        src: String,
        dst_parent_token: Handle,
        dst: String,
        responder: R,
    ) -> Result<(), fidl::Error>
    where
        R: FnOnce(Status) -> Result<(), fidl::Error>,
    {
        if self.base.flags & OPEN_RIGHT_WRITABLE == 0 {
            return responder(Status::BAD_HANDLE);
        }

        let src = match Path::validate_and_split(src) {
            Ok(src) => src,
            Err(status) => return responder(status),
        };
        let dst = match Path::validate_and_split(dst) {
            Ok(dst) => dst,
            Err(status) => return responder(status),
        };

        if !src.is_single_component() || !dst.is_single_component() {
            return responder(Status::INVALID_ARGS);
        }

        let token_registry = match self.base.scope.token_registry() {
            None => return responder(Status::NOT_SUPPORTED),
            Some(registry) => registry,
        };

        let dst_parent = match token_registry.get_container(dst_parent_token) {
            Err(status) => return responder(status),
            Ok(None) => return responder(Status::NOT_FOUND),
            Ok(Some(entry)) => entry,
        };

        match self
            .base
            .directory
            .clone()
            .into_mutable_directory()
            .get_filesystem()
            .rename(
                self.base.directory.clone().into_mutable_directory().into_any(),
                src,
                dst_parent.into_mutable_directory().into_any(),
                dst,
            )
            .await
        {
            Ok(()) => responder(Status::OK),
            Err(status) => responder(status),
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            directory::{
                dirents_sink,
                entry::{DirectoryEntry, EntryInfo},
                entry_container::*,
                traversal_position::TraversalPosition,
            },
            filesystem::{Filesystem, FilesystemRename},
            path::Path,
            registry::token_registry,
        },
        async_trait::async_trait,
        fidl_fuchsia_io::{
            DirectoryMarker, DirectoryProxy, NodeAttributes, DIRENT_TYPE_DIRECTORY,
            NODE_ATTRIBUTE_FLAG_CREATION_TIME, NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME,
            OPEN_RIGHT_READABLE,
        },
        fuchsia_async as fasync,
        std::{
            any::Any,
            sync::{Arc, Mutex, Weak},
        },
        storage_device::{
            buffer::Buffer,
            buffer_allocator::{BufferAllocator, MemBufferSource},
        },
    };

    #[derive(Debug, PartialEq)]
    enum MutableDirectoryAction {
        Link { id: u32, path: String },
        Unlink { id: u32, name: String },
        Rename { id: u32, src_name: String, dst_dir: Arc<MockDirectory>, dst_name: String },
        SetAttr { id: u32, flags: u32, attrs: NodeAttributes },
        Sync,
        Close,
    }

    #[derive(Debug)]
    struct MockDirectory {
        id: u32,
        fs: Arc<MockFilesystem>,
    }

    impl MockDirectory {
        pub fn new(id: u32, fs: Arc<MockFilesystem>) -> Arc<Self> {
            Arc::new(MockDirectory { id, fs })
        }
    }

    impl PartialEq for MockDirectory {
        fn eq(&self, other: &Self) -> bool {
            self.id == other.id
        }
    }

    impl DirectoryEntry for MockDirectory {
        fn open(
            self: Arc<Self>,
            _scope: ExecutionScope,
            _flags: u32,
            _mode: u32,
            _path: Path,
            _server_end: ServerEnd<NodeMarker>,
        ) {
            panic!("Not implemented!");
        }

        fn entry_info(&self) -> EntryInfo {
            EntryInfo::new(0, DIRENT_TYPE_DIRECTORY)
        }

        fn can_hardlink(&self) -> bool {
            // So we can use "self" in Directory::get_entry()
            // and expect link() to succeed.
            true
        }
    }

    #[async_trait]
    impl Directory for MockDirectory {
        fn get_entry<'a>(self: Arc<Self>, _name: &'a str) -> AsyncGetEntry<'a> {
            AsyncGetEntry::Immediate(Ok(self))
        }

        async fn read_dirents<'a>(
            &'a self,
            _pos: &'a TraversalPosition,
            _sink: Box<dyn dirents_sink::Sink>,
        ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), Status> {
            panic!("Not implemented");
        }

        fn register_watcher(
            self: Arc<Self>,
            _scope: ExecutionScope,
            _mask: u32,
            _channel: fasync::Channel,
        ) -> Result<(), Status> {
            panic!("Not implemented");
        }

        async fn get_attrs(&self) -> Result<NodeAttributes, Status> {
            panic!("Not implemented");
        }

        fn unregister_watcher(self: Arc<Self>, _key: usize) {
            panic!("Not implemented");
        }

        fn close(&self) -> Result<(), Status> {
            self.fs.handle_event(MutableDirectoryAction::Close)
        }
    }

    #[async_trait]
    impl MutableDirectory for MockDirectory {
        async fn link(&self, path: String, _entry: Arc<dyn DirectoryEntry>) -> Result<(), Status> {
            self.fs.handle_event(MutableDirectoryAction::Link { id: self.id, path })
        }

        async fn unlink(&self, name: &str, _must_be_directory: bool) -> Result<(), Status> {
            self.fs.handle_event(MutableDirectoryAction::Unlink {
                id: self.id,
                name: name.to_string(),
            })
        }

        async fn set_attrs(&self, flags: u32, attrs: NodeAttributes) -> Result<(), Status> {
            self.fs.handle_event(MutableDirectoryAction::SetAttr { id: self.id, flags, attrs })
        }

        fn get_filesystem(&self) -> &dyn Filesystem {
            &*self.fs
        }

        async fn sync(&self) -> Result<(), Status> {
            self.fs.handle_event(MutableDirectoryAction::Sync)
        }
    }

    struct Events(Mutex<Vec<MutableDirectoryAction>>);

    impl Events {
        fn new() -> Arc<Self> {
            Arc::new(Events(Mutex::new(vec![])))
        }
    }

    struct MockFilesystem {
        cur_id: Mutex<u32>,
        scope: ExecutionScope,
        events: Weak<Events>,
        buffer_allocator: BufferAllocator,
    }

    impl MockFilesystem {
        pub fn new(events: &Arc<Events>) -> Self {
            let token_registry = token_registry::Simple::new();
            let scope = ExecutionScope::build().token_registry(token_registry).new();
            let buffer_allocator =
                BufferAllocator::new(512, Box::new(MemBufferSource::new(1024 * 1024)));
            MockFilesystem {
                cur_id: Mutex::new(0),
                scope,
                events: Arc::downgrade(events),
                buffer_allocator,
            }
        }

        pub fn handle_event(&self, event: MutableDirectoryAction) -> Result<(), Status> {
            self.events.upgrade().map(|x| x.0.lock().unwrap().push(event));
            Ok(())
        }

        pub fn make_connection(
            self: &Arc<Self>,
            flags: u32,
        ) -> (Arc<MockDirectory>, DirectoryProxy) {
            let mut cur_id = self.cur_id.lock().unwrap();
            let dir = MockDirectory::new(*cur_id, self.clone());
            *cur_id += 1;
            let (proxy, server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>().unwrap();
            MutableConnection::create_connection(
                self.scope.clone(),
                OpenDirectory::new(dir.clone()),
                flags,
                server_end.into_channel().into(),
            );
            (dir, proxy)
        }
    }

    #[async_trait]
    impl FilesystemRename for MockFilesystem {
        async fn rename(
            &self,
            src_dir: Arc<Any + Sync + Send + 'static>,
            src_name: Path,
            dst_dir: Arc<Any + Sync + Send + 'static>,
            dst_name: Path,
        ) -> Result<(), Status> {
            let src_dir = src_dir.downcast::<MockDirectory>().unwrap();
            let dst_dir = dst_dir.downcast::<MockDirectory>().unwrap();
            self.handle_event(MutableDirectoryAction::Rename {
                id: src_dir.id,
                src_name: src_name.into_string(),
                dst_dir,
                dst_name: dst_name.into_string(),
            })
        }
    }

    impl Filesystem for MockFilesystem {
        fn block_size(&self) -> u32 {
            self.buffer_allocator.block_size() as u32
        }
        fn allocate_buffer(&self, size: usize) -> Buffer<'_> {
            self.buffer_allocator.allocate_buffer(size)
        }
    }

    impl std::fmt::Debug for MockFilesystem {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.debug_struct("MockFilesystem").field("cur_id", &self.cur_id).finish()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rename() {
        use fuchsia_zircon::Event;

        let events = Events::new();
        let fs = Arc::new(MockFilesystem::new(&events));

        let (_dir, proxy) = fs.clone().make_connection(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE);
        let (dir2, proxy2) = fs.clone().make_connection(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE);

        let (status, token) = proxy2.get_token().await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);

        let status = proxy.rename2("src", Event::from(token.unwrap()), "dest").await.unwrap();
        assert!(status.is_ok());

        let events = events.0.lock().unwrap();
        assert_eq!(
            *events,
            vec![MutableDirectoryAction::Rename {
                id: 0,
                src_name: "src".to_owned(),
                dst_dir: dir2,
                dst_name: "dest".to_owned(),
            },]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_setattr() {
        let events = Events::new();
        let fs = Arc::new(MockFilesystem::new(&events));
        let (_dir, proxy) = fs.clone().make_connection(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE);
        let mut attrs = NodeAttributes {
            mode: 0,
            id: 0,
            content_size: 0,
            storage_size: 0,
            link_count: 0,
            creation_time: 30,
            modification_time: 100,
        };
        let status = proxy
            .set_attr(
                NODE_ATTRIBUTE_FLAG_CREATION_TIME | NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME,
                &mut attrs,
            )
            .await
            .unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);

        let events = events.0.lock().unwrap();
        assert_eq!(
            *events,
            vec![MutableDirectoryAction::SetAttr {
                id: 0,
                flags: NODE_ATTRIBUTE_FLAG_CREATION_TIME | NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME,
                attrs
            }]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_link() {
        let events = Events::new();
        let fs = Arc::new(MockFilesystem::new(&events));
        let (_dir, proxy) = fs.clone().make_connection(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE);
        let (_dir2, proxy2) = fs.clone().make_connection(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE);

        let (status, token) = proxy2.get_token().await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);

        let status = proxy.link("src", token.unwrap(), "dest").await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        let events = events.0.lock().unwrap();
        assert_eq!(*events, vec![MutableDirectoryAction::Link { id: 1, path: "dest".to_owned() },]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unlink() {
        let events = Events::new();
        let fs = Arc::new(MockFilesystem::new(&events));
        let (_dir, proxy) = fs.clone().make_connection(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE);
        proxy
            .unlink2("test", UnlinkOptions::EMPTY)
            .await
            .expect("fidl call failed")
            .expect("unlink failed");
        let events = events.0.lock().unwrap();
        assert_eq!(
            *events,
            vec![MutableDirectoryAction::Unlink { id: 0, name: "test".to_string() },]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_sync() {
        let events = Events::new();
        let fs = Arc::new(MockFilesystem::new(&events));
        let (_dir, proxy) = fs.clone().make_connection(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE);
        let status = proxy.sync().await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        let events = events.0.lock().unwrap();
        assert_eq!(*events, vec![MutableDirectoryAction::Sync]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_close() {
        let events = Events::new();
        let fs = Arc::new(MockFilesystem::new(&events));
        let (_dir, proxy) = fs.clone().make_connection(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE);
        let status = proxy.close().await.unwrap();
        assert_eq!(Status::from_raw(status), Status::OK);
        let events = events.0.lock().unwrap();
        assert_eq!(*events, vec![MutableDirectoryAction::Close]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_implicit_close() {
        let events = Events::new();
        let fs = Arc::new(MockFilesystem::new(&events));
        let (_dir, _proxy) = fs.clone().make_connection(OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE);

        fs.scope.shutdown();
        fs.scope.wait().await;

        let events = events.0.lock().unwrap();
        assert_eq!(*events, vec![MutableDirectoryAction::Close]);
    }
}
