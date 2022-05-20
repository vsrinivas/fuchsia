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
    anyhow::{bail, Error},
    either::{Either, Left, Right},
    fidl::{endpoints::ServerEnd, Handle},
    fidl_fuchsia_io as fio, fuchsia_zircon as zx,
    futures::{channel::oneshot, future::BoxFuture},
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

    fn new(
        scope: ExecutionScope,
        directory: OpenDirectory<Self::Directory>,
        flags: fio::OpenFlags,
    ) -> Self {
        MutableConnection { base: BaseConnection::<Self>::new(scope, directory, flags) }
    }

    fn create_connection(
        scope: ExecutionScope,
        directory: Arc<Self::Directory>,
        flags: fio::OpenFlags,
        server_end: ServerEnd<fio::NodeMarker>,
    ) {
        if let Ok((connection, requests)) =
            Self::prepare_connection(scope.clone(), directory, flags, server_end)
        {
            // If we fail to send the task to the executor, it is probably shut down or is in the
            // process of shutting down (this is the only error state currently).  So there is
            // nothing for us to do - the connection will be closed automatically when the
            // connection object is dropped.
            let _ = scope.spawn_with_shutdown(move |shutdown| {
                connection.handle_requests(requests, shutdown)
            });
        }
    }

    fn entry_not_found(
        scope: ExecutionScope,
        parent: Arc<dyn DirectoryEntry>,
        flags: fio::OpenFlags,
        mode: u32,
        name: &str,
        path: &Path,
    ) -> Result<Arc<dyn DirectoryEntry>, zx::Status> {
        if !flags.intersects(fio::OpenFlags::CREATE) {
            return Err(zx::Status::NOT_FOUND);
        }

        let type_ = NewEntryType::from_flags_and_mode(flags, mode, path.is_dir())?;

        let entry_constructor = match scope.entry_constructor() {
            None => return Err(zx::Status::NOT_SUPPORTED),
            Some(constructor) => constructor,
        };

        entry_constructor.create_entry(parent, type_, name, path)
    }

    fn handle_request(
        &mut self,
        request: fio::DirectoryRequest,
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
    /// Very similar to create_connection, but creates a connection without spawning a new task.
    pub async fn create_connection_async(
        scope: ExecutionScope,
        directory: Arc<dyn MutableConnectionClient>,
        flags: fio::OpenFlags,
        server_end: ServerEnd<fio::NodeMarker>,
        shutdown: oneshot::Receiver<()>,
    ) {
        if let Ok((connection, requests)) =
            Self::prepare_connection(scope, directory, flags, server_end)
        {
            connection.handle_requests(requests, shutdown).await;
        }
    }

    async fn handle_request(
        &mut self,
        request: fio::DirectoryRequest,
    ) -> Result<Either<ConnectionState, fio::DirectoryRequest>, Error> {
        match request {
            fio::DirectoryRequest::Unlink { name, options, responder } => {
                let result = self.handle_unlink(name, options).await;
                responder.send(&mut result.map_err(zx::Status::into_raw))?;
            }
            fio::DirectoryRequest::GetToken { responder } => {
                let (status, token) = match self.handle_get_token() {
                    Ok(token) => (zx::Status::OK, Some(token)),
                    Err(status) => (status, None),
                };
                responder.send(status.into_raw(), token)?;
            }
            fio::DirectoryRequest::Rename { src, dst_parent_token, dst, responder } => {
                let result = self.handle_rename(src, Handle::from(dst_parent_token), dst).await;
                responder.send(&mut result.map_err(zx::Status::into_raw))?;
            }
            fio::DirectoryRequest::SetAttr { flags, attributes, responder } => {
                let status = match self.handle_setattr(flags, attributes).await {
                    Ok(()) => zx::Status::OK,
                    Err(status) => status,
                };
                responder.send(status.into_raw())?;
            }
            fio::DirectoryRequest::SyncDeprecated { responder } => {
                responder.send(
                    self.base.directory.sync().await.err().unwrap_or(zx::Status::OK).into_raw(),
                )?;
            }
            fio::DirectoryRequest::Sync { responder } => {
                responder
                    .send(&mut self.base.directory.sync().await.map_err(zx::Status::into_raw))?;
            }
            // TODO(https:/fxbug.dev/77623): which other io2 methods need to be implemented here?
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
        flags: fio::NodeAttributeFlags,
        attributes: fio::NodeAttributes,
    ) -> Result<(), zx::Status> {
        if !self.base.flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
            return Err(zx::Status::BAD_HANDLE);
        }

        // TODO(jfsulliv): Consider always permitting attributes to be deferrable. The risk with
        // this is that filesystems would require a background flush of dirty attributes to disk.
        self.base.directory.set_attrs(flags, attributes).await
    }

    async fn handle_unlink(
        &mut self,
        name: String,
        options: fio::UnlinkOptions,
    ) -> Result<(), zx::Status> {
        if !self.base.flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
            return Err(zx::Status::BAD_HANDLE);
        }

        if name.is_empty() || name.contains('/') || name == "." || name == ".." {
            return Err(zx::Status::INVALID_ARGS);
        }

        self.base
            .directory
            .clone()
            .unlink(
                &name,
                options
                    .flags
                    .map(|f| f.contains(fio::UnlinkFlags::MUST_BE_DIRECTORY))
                    .unwrap_or(false),
            )
            .await
    }

    fn handle_get_token(&self) -> Result<Handle, zx::Status> {
        if !self.base.flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
            return Err(zx::Status::BAD_HANDLE);
        }

        let token_registry = match self.base.scope.token_registry() {
            None => return Err(zx::Status::NOT_SUPPORTED),
            Some(registry) => registry,
        };

        let token =
            token_registry.get_token(self.base.directory.clone().into_token_registry_client())?;
        Ok(token)
    }

    async fn handle_rename(
        &self,
        src: String,
        dst_parent_token: Handle,
        dst: String,
    ) -> Result<(), zx::Status> {
        if !self.base.flags.intersects(fio::OpenFlags::RIGHT_WRITABLE) {
            return Err(zx::Status::BAD_HANDLE);
        }

        let src = Path::validate_and_split(src)?;
        let dst = Path::validate_and_split(dst)?;

        if !src.is_single_component() || !dst.is_single_component() {
            return Err(zx::Status::INVALID_ARGS);
        }

        let token_registry = match self.base.scope.token_registry() {
            None => return Err(zx::Status::NOT_SUPPORTED),
            Some(registry) => registry,
        };

        let dst_parent = match token_registry.get_container(dst_parent_token)? {
            None => return Err(zx::Status::NOT_FOUND),
            Some(entry) => entry,
        };

        self.base
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
    }

    fn prepare_connection(
        scope: ExecutionScope,
        directory: Arc<dyn MutableConnectionClient>,
        flags: fio::OpenFlags,
        server_end: ServerEnd<fio::NodeMarker>,
    ) -> Result<(Self, fio::DirectoryRequestStream), Error> {
        // Ensure we close the directory if we fail to prepare the connection.
        let directory = OpenDirectory::new(directory);

        // TODO(fxbug.dev/82054): These flags should be validated before prepare_connection is called
        // since at this point the directory resource has already been opened/created.
        let flags = match new_connection_validate_flags(flags) {
            Ok(updated) => updated,
            Err(status) => {
                send_on_open_with_error(flags, server_end, status);
                bail!(status);
            }
        };

        let (requests, control_handle) =
            ServerEnd::<fio::DirectoryMarker>::new(server_end.into_channel())
                .into_stream_and_control_handle()?;

        if flags.intersects(fio::OpenFlags::DESCRIBE) {
            let mut info = fio::NodeInfo::Directory(fio::DirectoryObject);
            control_handle.send_on_open_(zx::Status::OK.into_raw(), Some(&mut info))?;
        }

        Ok((Self::new(scope, directory, flags), requests))
    }

    async fn handle_requests(
        self,
        requests: fio::DirectoryRequestStream,
        shutdown: oneshot::Receiver<()>,
    ) {
        handle_requests::<Self>(requests, self, shutdown).await;
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
                entry_container::{Directory, DirectoryWatcher},
                traversal_position::TraversalPosition,
            },
            filesystem::{Filesystem, FilesystemRename},
            path::Path,
            registry::token_registry,
        },
        async_trait::async_trait,
        fuchsia_async as fasync,
        std::{
            any::Any,
            sync::{Arc, Mutex, Weak},
        },
    };

    #[derive(Debug, PartialEq)]
    enum MutableDirectoryAction {
        Link { id: u32, path: String },
        Unlink { id: u32, name: String },
        Rename { id: u32, src_name: String, dst_dir: Arc<MockDirectory>, dst_name: String },
        SetAttr { id: u32, flags: fio::NodeAttributeFlags, attrs: fio::NodeAttributes },
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
            _flags: fio::OpenFlags,
            _mode: u32,
            _path: Path,
            _server_end: ServerEnd<fio::NodeMarker>,
        ) {
            panic!("Not implemented!");
        }

        fn entry_info(&self) -> EntryInfo {
            EntryInfo::new(0, fio::DirentType::Directory)
        }
    }

    #[async_trait]
    impl Directory for MockDirectory {
        async fn read_dirents<'a>(
            &'a self,
            _pos: &'a TraversalPosition,
            _sink: Box<dyn dirents_sink::Sink>,
        ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), zx::Status> {
            panic!("Not implemented");
        }

        fn register_watcher(
            self: Arc<Self>,
            _scope: ExecutionScope,
            _mask: fio::WatchMask,
            _watcher: DirectoryWatcher,
        ) -> Result<(), zx::Status> {
            panic!("Not implemented");
        }

        async fn get_attrs(&self) -> Result<fio::NodeAttributes, zx::Status> {
            panic!("Not implemented");
        }

        fn unregister_watcher(self: Arc<Self>, _key: usize) {
            panic!("Not implemented");
        }

        fn close(&self) -> Result<(), zx::Status> {
            self.fs.handle_event(MutableDirectoryAction::Close)
        }
    }

    #[async_trait]
    impl MutableDirectory for MockDirectory {
        async fn link(
            self: Arc<Self>,
            path: String,
            _source_dir: Arc<dyn Any + Send + Sync>,
            _source_name: &str,
        ) -> Result<(), zx::Status> {
            self.fs.handle_event(MutableDirectoryAction::Link { id: self.id, path })
        }

        async fn unlink(
            self: Arc<Self>,
            name: &str,
            _must_be_directory: bool,
        ) -> Result<(), zx::Status> {
            self.fs.handle_event(MutableDirectoryAction::Unlink {
                id: self.id,
                name: name.to_string(),
            })
        }

        async fn set_attrs(
            &self,
            flags: fio::NodeAttributeFlags,
            attrs: fio::NodeAttributes,
        ) -> Result<(), zx::Status> {
            self.fs.handle_event(MutableDirectoryAction::SetAttr { id: self.id, flags, attrs })
        }

        fn get_filesystem(&self) -> &dyn Filesystem {
            &*self.fs
        }

        async fn sync(&self) -> Result<(), zx::Status> {
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
    }

    impl MockFilesystem {
        pub fn new(events: &Arc<Events>) -> Self {
            let token_registry = token_registry::Simple::new();
            let scope = ExecutionScope::build().token_registry(token_registry).new();
            MockFilesystem { cur_id: Mutex::new(0), scope, events: Arc::downgrade(events) }
        }

        pub fn handle_event(&self, event: MutableDirectoryAction) -> Result<(), zx::Status> {
            self.events.upgrade().map(|x| x.0.lock().unwrap().push(event));
            Ok(())
        }

        pub fn make_connection(
            self: &Arc<Self>,
            flags: fio::OpenFlags,
        ) -> (Arc<MockDirectory>, fio::DirectoryProxy) {
            let mut cur_id = self.cur_id.lock().unwrap();
            let dir = MockDirectory::new(*cur_id, self.clone());
            *cur_id += 1;
            let (proxy, server_end) =
                fidl::endpoints::create_proxy::<fio::DirectoryMarker>().unwrap();
            MutableConnection::create_connection(
                self.scope.clone(),
                dir.clone(),
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
        ) -> Result<(), zx::Status> {
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
            512
        }
    }

    impl std::fmt::Debug for MockFilesystem {
        fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
            f.debug_struct("MockFilesystem").field("cur_id", &self.cur_id).finish()
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_rename() {
        use zx::Event;

        let events = Events::new();
        let fs = Arc::new(MockFilesystem::new(&events));

        let (_dir, proxy) = fs
            .clone()
            .make_connection(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);
        let (dir2, proxy2) = fs
            .clone()
            .make_connection(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);

        let (status, token) = proxy2.get_token().await.unwrap();
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);

        let status = proxy.rename("src", Event::from(token.unwrap()), "dest").await.unwrap();
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
        let (_dir, proxy) = fs
            .clone()
            .make_connection(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);
        let mut attrs = fio::NodeAttributes {
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
                fio::NodeAttributeFlags::CREATION_TIME | fio::NodeAttributeFlags::MODIFICATION_TIME,
                &mut attrs,
            )
            .await
            .unwrap();
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);

        let events = events.0.lock().unwrap();
        assert_eq!(
            *events,
            vec![MutableDirectoryAction::SetAttr {
                id: 0,
                flags: fio::NodeAttributeFlags::CREATION_TIME
                    | fio::NodeAttributeFlags::MODIFICATION_TIME,
                attrs
            }]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_link() {
        let events = Events::new();
        let fs = Arc::new(MockFilesystem::new(&events));
        let (_dir, proxy) = fs
            .clone()
            .make_connection(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);
        let (_dir2, proxy2) = fs
            .clone()
            .make_connection(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);

        let (status, token) = proxy2.get_token().await.unwrap();
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);

        let status = proxy.link("src", token.unwrap(), "dest").await.unwrap();
        assert_eq!(zx::Status::from_raw(status), zx::Status::OK);
        let events = events.0.lock().unwrap();
        assert_eq!(*events, vec![MutableDirectoryAction::Link { id: 1, path: "dest".to_owned() },]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unlink() {
        let events = Events::new();
        let fs = Arc::new(MockFilesystem::new(&events));
        let (_dir, proxy) = fs
            .clone()
            .make_connection(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);
        proxy
            .unlink("test", fio::UnlinkOptions::EMPTY)
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
        let (_dir, proxy) = fs
            .clone()
            .make_connection(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);
        let () = proxy.sync().await.unwrap().map_err(zx::Status::from_raw).unwrap();
        let events = events.0.lock().unwrap();
        assert_eq!(*events, vec![MutableDirectoryAction::Sync]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_close() {
        let events = Events::new();
        let fs = Arc::new(MockFilesystem::new(&events));
        let (_dir, proxy) = fs
            .clone()
            .make_connection(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);
        let () = proxy.close().await.unwrap().map_err(zx::Status::from_raw).unwrap();
        let events = events.0.lock().unwrap();
        assert_eq!(*events, vec![MutableDirectoryAction::Close]);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_implicit_close() {
        let events = Events::new();
        let fs = Arc::new(MockFilesystem::new(&events));
        let (_dir, _proxy) = fs
            .clone()
            .make_connection(fio::OpenFlags::RIGHT_READABLE | fio::OpenFlags::RIGHT_WRITABLE);

        fs.scope.shutdown();
        fs.scope.wait().await;

        let events = events.0.lock().unwrap();
        assert_eq!(*events, vec![MutableDirectoryAction::Close]);
    }
}
