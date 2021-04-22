// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        object_store::{
            self,
            directory::{self, ObjectDescriptor},
            transaction::{LockKey, Transaction},
            INVALID_OBJECT_ID,
        },
        server::{
            errors::map_to_status,
            file::FxFile,
            node::{FxNode, GetResult},
            volume::FxVolume,
        },
    },
    anyhow::{bail, Error},
    async_trait::async_trait,
    either::{Left, Right},
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{
        self as fio, NodeAttributes, NodeMarker, MODE_TYPE_DIRECTORY, OPEN_FLAG_CREATE,
        OPEN_FLAG_CREATE_IF_ABSENT,
    },
    fuchsia_async as fasync,
    fuchsia_zircon::Status,
    std::{
        any::Any,
        sync::{
            atomic::{AtomicBool, Ordering},
            Arc,
        },
    },
    vfs::{
        common::send_on_open_with_error,
        directory::{
            connection::{io1::DerivedConnection, util::OpenDirectory},
            dirents_sink::{self, Sink},
            entry::{DirectoryEntry, EntryInfo},
            entry_container::{AsyncGetEntry, Directory, MutableDirectory},
            mutable::connection::io1::MutableConnection,
            traversal_position::TraversalPosition,
        },
        execution_scope::ExecutionScope,
        filesystem::Filesystem,
        path::Path,
    },
};

pub struct FxDirectory {
    volume: Arc<FxVolume>,
    directory: object_store::Directory,
    is_deleted: AtomicBool,
}

impl FxDirectory {
    pub(super) fn new(volume: Arc<FxVolume>, directory: object_store::Directory) -> Self {
        Self { volume, directory, is_deleted: AtomicBool::new(false) }
    }

    fn ensure_writable(&self) -> Result<(), Error> {
        if self.is_deleted.load(Ordering::Relaxed) {
            bail!(FxfsError::Deleted)
        } else {
            Ok(())
        }
    }

    async fn lookup(
        self: &Arc<Self>,
        flags: u32,
        mode: u32,
        mut path: Path,
    ) -> Result<Arc<dyn FxNode>, Error> {
        let store = self.volume.store();
        let fs = store.filesystem();
        let mut current_node = self.clone() as Arc<dyn FxNode>;
        while !path.is_empty() {
            let last_segment = path.is_single_component();
            let current_dir =
                current_node.into_any().downcast::<FxDirectory>().map_err(|_| FxfsError::NotDir)?;
            let name = path.next().unwrap();

            // Create the transaction here if we might need to create the object so that we have a
            // lock in place.
            let keys =
                [LockKey::object(store.store_object_id(), current_dir.directory.object_id())];
            let transaction_or_guard = if last_segment && flags & OPEN_FLAG_CREATE != 0 {
                Left(fs.clone().new_transaction(&keys).await?)
            } else {
                // When child objects are created, the object is created along with the directory
                // entry in the same transaction, and so we need to hold a read lock over the lookup
                // and open calls.
                Right(fs.read_lock(&keys).await)
            };

            current_node = match current_dir.directory.lookup(name).await {
                Ok((object_id, object_descriptor)) => {
                    if transaction_or_guard.is_left() && flags & OPEN_FLAG_CREATE_IF_ABSENT != 0 {
                        bail!(FxfsError::AlreadyExists);
                    }
                    self.volume.get_or_load_node(object_id, object_descriptor).await?
                }
                Err(e) if FxfsError::NotFound.matches(&e) => {
                    if let Left(mut transaction) = transaction_or_guard {
                        let node = current_dir.create_child(&mut transaction, name, mode).await?;
                        if let GetResult::Placeholder(p) =
                            self.volume.cache().get_or_reserve(node.object_id()).await
                        {
                            p.commit(&node);
                            transaction.commit().await;
                        } else {
                            // We created a node, but the object ID was already used in the cache,
                            // which suggests a object ID was reused (which would either be a bug or
                            // corruption).
                            bail!(FxfsError::Inconsistent);
                        }
                        node
                    } else {
                        bail!(e)
                    }
                }
                Err(e) => bail!(e),
            };
        }
        Ok(current_node)
    }

    async fn create_child(
        self: &Arc<Self>,
        transaction: &mut Transaction<'_>,
        name: &str,
        mode: u32,
    ) -> Result<Arc<dyn FxNode>, Error> {
        self.ensure_writable()?;
        if mode & MODE_TYPE_DIRECTORY != 0 {
            let dir = self.directory.create_child_dir(transaction, name).await?;
            Ok(Arc::new(FxDirectory::new(self.volume.clone(), dir)) as Arc<dyn FxNode>)
        } else {
            let handle = self.directory.create_child_file(transaction, name).await?;
            Ok(Arc::new(FxFile::new(handle, self.volume.clone())) as Arc<dyn FxNode>)
        }
    }

    async fn unlink_inner(self: &Arc<Self>, name: &str) -> Result<(), Error> {
        // Acquire a transaction with the appropriate locks.
        // We always need to lock |self|, but we only need to lock the child if it's a directory,
        // to prevent entries being added to the directory.
        // Since we don't know the child object ID until we've looked up the child, we need to loop
        // until we have acquired a lock on a child whose ID is the same as it was in the last
        // iteration (or the child is a file, at which point it doesn't matter).
        //
        // Note that the returned transaction may lock more objects than is necessary (for example,
        // if the child "foo" was first a directory, then was renamed to "bar" and a file "foo" was
        // created, we might acquire a lock on both the parent and "bar").
        let store = self.volume.store();
        let mut child_object_id = INVALID_OBJECT_ID;
        let (mut transaction, object_descriptor) = loop {
            let lock_keys = if child_object_id == INVALID_OBJECT_ID {
                vec![LockKey::object(store.store_object_id(), self.object_id())]
            } else {
                vec![
                    LockKey::object(store.store_object_id(), self.object_id()),
                    LockKey::object(store.store_object_id(), child_object_id),
                ]
            };
            let fs = store.filesystem().clone();
            let transaction = fs.new_transaction(&lock_keys).await?;

            let (object_id, object_descriptor) = self.directory.lookup(name).await?;
            match object_descriptor {
                ObjectDescriptor::File => {
                    child_object_id = object_id;
                    break (transaction, object_descriptor);
                }
                ObjectDescriptor::Directory => {
                    if object_id == child_object_id {
                        break (transaction, object_descriptor);
                    }
                    child_object_id = object_id;
                }
                ObjectDescriptor::Volume(_) => bail!(FxfsError::Inconsistent),
            }
        };

        // TODO(jfsulliv): We can immediately delete files if they have no open references, but
        // we need appropriate locking in place to be able to check the file's open count.
        let _dir = if let ObjectDescriptor::File = object_descriptor {
            directory::move_child(
                &mut transaction,
                &self.directory,
                &name,
                self.volume.graveyard(),
                &format!("{}", child_object_id),
            )
            .await?;
            None
        } else {
            // For directories, we need to set |is_deleted| on the in-memory node. If the node's
            // absent from the cache, we *must* load the node into memory, and make sure the node
            // stays in the cache until we commit the transaction, because otherwise another caller
            // could load the node from disk before we commit the transaction and would not see that
            // it's been unlinked.
            // Holding a placeholder here could deadlock, since transaction.commit() will block
            // until there are no readers, but a reader could be blocked on the cache by the
            // placeholder.
            // TODO(csuter): Separate out a commit_prepare which acquires the lock, so that we can
            // handle situations like this more gracefully. (Alternatively, add a callback to
            // commit.)
            self.directory.remove_child(&mut transaction, &name).await?;
            let dir = self
                .volume
                .get_or_load_node(child_object_id, object_descriptor)
                .await?
                .into_any()
                .downcast::<FxDirectory>()
                .unwrap();
            dir.is_deleted.store(true, Ordering::Relaxed);
            Some(dir)
        };
        transaction.commit().await;

        Ok(())
    }

    // TODO(jfsulliv): Change the VFS to send in &Arc<Self> so we don't need this.
    async fn as_strong(&self) -> Arc<Self> {
        self.volume
            .get_or_load_node(self.object_id(), ObjectDescriptor::Directory)
            .await
            .expect("open_or_load_node on self failed")
            .into_any()
            .downcast::<FxDirectory>()
            .unwrap()
    }
}

impl Drop for FxDirectory {
    fn drop(&mut self) {
        self.volume.cache().remove(self.object_id());
    }
}

impl FxNode for FxDirectory {
    fn object_id(&self) -> u64 {
        self.directory.object_id()
    }
    fn into_any(self: Arc<Self>) -> Arc<dyn Any + Send + Sync + 'static> {
        self
    }
}

#[async_trait]
impl MutableDirectory for FxDirectory {
    async fn link(&self, _name: String, _entry: Arc<dyn DirectoryEntry>) -> Result<(), Status> {
        log::error!("link not implemented");
        Err(Status::NOT_SUPPORTED)
    }

    async fn unlink(&self, path: Path) -> Result<(), Status> {
        if !path.is_single_component() {
            // The VFS is supposed to handle traversal. Per fxbug.dev/74544, it does not, but that
            // ought to be fixed in the VFS rather than here.
            return Err(Status::BAD_PATH);
        }
        self.as_strong().await.unlink_inner(path.peek().unwrap()).await.map_err(map_to_status)
    }

    async fn set_attrs(&self, _flags: u32, _attrs: NodeAttributes) -> Result<(), Status> {
        log::error!("set_attrs not implemented");
        Err(Status::NOT_SUPPORTED)
    }

    fn get_filesystem(&self) -> &dyn Filesystem {
        &*self.volume
    }

    fn into_any(self: Arc<Self>) -> Arc<dyn Any + Sync + Send> {
        self as Arc<dyn Any + Sync + Send>
    }

    async fn sync(&self) -> Result<(), Status> {
        // TODO(csuter): Support sync on root of fxfs volume.
        Ok(())
    }
}

impl DirectoryEntry for FxDirectory {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        let cloned_scope = scope.clone();
        scope.spawn(async move {
            // TODO(jfsulliv): Factor this out into a visitor-pattern style method for FxNode, e.g.
            // FxNode::visit(FileFn, DirFn).
            match self.lookup(flags, mode, path).await {
                Err(e) => send_on_open_with_error(flags, server_end, map_to_status(e)),
                Ok(node) => {
                    if let Ok(dir) = node.clone().into_any().downcast::<FxDirectory>() {
                        MutableConnection::create_connection(
                            cloned_scope,
                            OpenDirectory::new(dir),
                            flags,
                            mode,
                            server_end,
                        );
                    } else if let Ok(file) = node.into_any().downcast::<FxFile>() {
                        file.clone().open(cloned_scope, flags, mode, Path::empty(), server_end);
                    } else {
                        unreachable!();
                    }
                }
            };
        });
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DIRENT_TYPE_DIRECTORY)
    }

    fn can_hardlink(&self) -> bool {
        false
    }
}

#[async_trait]
impl Directory for FxDirectory {
    fn get_entry(self: Arc<Self>, _name: String) -> AsyncGetEntry {
        // TODO(jfsulliv): Implement
        AsyncGetEntry::Immediate(Err(Status::NOT_FOUND))
    }

    async fn read_dirents<'a>(
        &'a self,
        _pos: &'a TraversalPosition,
        sink: Box<dyn Sink>,
    ) -> Result<(TraversalPosition, Box<dyn dirents_sink::Sealed>), Status> {
        // TODO(jfsulliv): Implement
        Ok((TraversalPosition::End, sink.seal()))
    }

    fn register_watcher(
        self: Arc<Self>,
        _scope: ExecutionScope,
        _mask: u32,
        _channel: fasync::Channel,
    ) -> Result<(), Status> {
        // TODO(jfsulliv): Implement
        Err(Status::NOT_SUPPORTED)
    }

    fn unregister_watcher(self: Arc<Self>, _key: usize) {
        // TODO(jfsulliv): Implement
    }

    async fn get_attrs(&self) -> Result<NodeAttributes, Status> {
        Err(Status::NOT_SUPPORTED)
    }

    fn close(&self) -> Result<(), Status> {
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            object_store::{filesystem::SyncOptions, FxFilesystem},
            server::{
                testing::{open_dir_validating, open_file, open_file_validating},
                volume::FxVolumeAndRoot,
            },
            testing::fake_device::FakeDevice,
            volume::root_volume,
        },
        anyhow::Error,
        fidl::endpoints::ServerEnd,
        fidl_fuchsia_io::{
            DirectoryMarker, SeekOrigin, MODE_TYPE_DIRECTORY, MODE_TYPE_FILE, OPEN_FLAG_CREATE,
            OPEN_FLAG_CREATE_IF_ABSENT, OPEN_FLAG_DIRECTORY, OPEN_RIGHT_READABLE,
            OPEN_RIGHT_WRITABLE,
        },
        fuchsia_async as fasync,
        fuchsia_zircon::Status,
        io_util::{read_file_bytes, write_file_bytes},
        matches::assert_matches,
        rand::Rng,
        std::{sync::Arc, time::Duration},
        vfs::{directory::entry::DirectoryEntry, execution_scope::ExecutionScope, path::Path},
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_lifecycle() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_volume = root_volume(&filesystem).await?;
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await?).await;
        {
            let (dir_proxy, dir_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>()
                .expect("Create proxy to succeed");

            vol.root().clone().open(
                ExecutionScope::new(),
                OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_DIRECTORY,
                Path::empty(),
                ServerEnd::new(dir_server_end.into_channel()),
            );

            open_dir_validating(
                &dir_proxy,
                OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE,
                MODE_TYPE_DIRECTORY,
                "foo",
            )
            .await
            .expect("Create dir failed");

            open_file_validating(
                &dir_proxy,
                OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE,
                MODE_TYPE_FILE,
                "bar",
            )
            .await
            .expect("Create file failed");

            dir_proxy.close().await?;
        }

        // Ensure that there's no remaining references to |vol|, which would indicate a reference
        // cycle or other leak.
        Arc::try_unwrap(vol.into_volume()).map_err(|_| "References to vol still exist").unwrap();

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_root_dir() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_volume = root_volume(&filesystem).await?;
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await?).await;
        let dir = vol.root().clone();

        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        dir_proxy.describe().await.expect("Describe to succeed");

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_create_dir_persists() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        for i in 0..2 {
            let (filesystem, vol) = if i == 0 {
                let filesystem = FxFilesystem::new_empty(device.clone()).await?;
                let root_volume = root_volume(&filesystem).await?;
                let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await?).await;
                (filesystem, vol)
            } else {
                let filesystem = FxFilesystem::open(device.clone()).await?;
                let root_volume = root_volume(&filesystem).await?;
                let vol = FxVolumeAndRoot::new(root_volume.volume("vol").await?).await;
                (filesystem, vol)
            };
            let dir = vol.root().clone();
            let (dir_proxy, dir_server_end) = fidl::endpoints::create_proxy::<DirectoryMarker>()
                .expect("Create proxy to succeed");

            dir.open(
                ExecutionScope::new(),
                OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_DIRECTORY,
                Path::empty(),
                ServerEnd::new(dir_server_end.into_channel()),
            );

            let flags =
                if i == 0 { OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE } else { OPEN_RIGHT_READABLE };
            open_dir_validating(&dir_proxy, flags, MODE_TYPE_DIRECTORY, "foo")
                .await
                .expect(&format!("Open dir iter {} failed", i));

            filesystem.sync(SyncOptions::default()).await?;
        }

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_open_nonexistent_file() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_volume = root_volume(&filesystem).await?;
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await?).await;
        let dir = vol.root().clone();

        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let child_proxy = open_file(&dir_proxy, OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "foo")
            .expect("Create proxy failed");

        // The channel also be closed with a NOT_FOUND epitaph.
        assert_matches!(
            child_proxy.describe().await,
            Err(fidl::Error::ClientChannelClosed {
                status: Status::NOT_FOUND,
                service_name: "(anonymous) File",
            })
        );

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_create_file() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_volume = root_volume(&filesystem).await?;
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await?).await;
        let dir = vol.root().clone();

        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        open_file_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE,
            MODE_TYPE_FILE,
            "foo",
        )
        .await
        .expect("Create file failed");

        open_file_validating(&dir_proxy, OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "foo")
            .await
            .expect("Open file failed");

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_create_dir_nested() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_volume = root_volume(&filesystem).await?;
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await?).await;
        let dir = vol.root().clone();

        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        open_dir_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await
        .expect("Create dir failed");

        open_dir_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE,
            MODE_TYPE_DIRECTORY,
            "foo/bar",
        )
        .await
        .expect("Create nested dir failed");

        open_dir_validating(&dir_proxy, OPEN_RIGHT_READABLE, MODE_TYPE_DIRECTORY, "foo/bar")
            .await
            .expect("Open nested dir failed");

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_strict_create_file_fails_if_present() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_volume = root_volume(&filesystem).await?;
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await?).await;
        let dir = vol.root().clone();

        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        open_file_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT | OPEN_RIGHT_READABLE,
            MODE_TYPE_FILE,
            "foo",
        )
        .await
        .expect("Create file failed");

        let file_proxy = open_file(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT | OPEN_RIGHT_READABLE,
            MODE_TYPE_FILE,
            "foo",
        )
        .expect("Open proxy failed");

        assert_matches!(
            file_proxy.describe().await,
            Err(fidl::Error::ClientChannelClosed {
                status: Status::ALREADY_EXISTS,
                service_name: "(anonymous) File",
            })
        );

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    #[ignore] // TODO(jfsulliv): Re-enable when we don't defer deleting files with 0 references.
    async fn test_unlink_file_with_no_refs_immediately_freed() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_volume = root_volume(&filesystem).await?;
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await?).await;
        let dir = vol.root().clone();

        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        {
            let file_proxy = open_file_validating(
                &dir_proxy,
                OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_FILE,
                "foo",
            )
            .await
            .expect("Create file failed");

            // Fill up the file with a lot of data, so we can verify that the extents are freed.
            let buf = vec![0xaa as u8; 512];
            loop {
                match write_file_bytes(&file_proxy, buf.as_slice()).await {
                    Ok(_) => {}
                    Err(e) => {
                        if let Some(status) = e.root_cause().downcast_ref::<Status>() {
                            if status == &Status::NO_SPACE {
                                break;
                            }
                        }
                        return Err(e);
                    }
                }
            }

            file_proxy.close().await?;
        }

        dir_proxy.unlink("foo").await.expect("unlink failed");

        assert_eq!(
            open_file_validating(&dir_proxy, OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "foo")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );

        // Create another file so we can verify that the extents were actually freed.
        let file_proxy = open_file_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_FILE,
            "bar",
        )
        .await
        .expect("Create file failed");
        let buf = vec![0xaa as u8; 8192];
        write_file_bytes(&file_proxy, buf.as_slice()).await.expect("Failed to write new file");

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unlink_file() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_volume = root_volume(&filesystem).await?;
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await?).await;
        let dir = vol.root().clone();

        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let file_proxy = open_file_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_FILE,
            "foo",
        )
        .await
        .expect("Create file failed");
        file_proxy.close().await.expect("close failed");

        Status::ok(dir_proxy.unlink("foo").await.expect("FIDL call failed"))
            .expect("unlink failed");

        assert_eq!(
            open_file_validating(&dir_proxy, OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "foo")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unlink_file_with_active_references() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_volume = root_volume(&filesystem).await?;
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await?).await;
        let dir = vol.root().clone();

        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let file_proxy = open_file_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_FILE,
            "foo",
        )
        .await
        .expect("Create file failed");

        let buf = vec![0xaa as u8; 512];
        write_file_bytes(&file_proxy, buf.as_slice()).await.expect("write failed");

        Status::ok(dir_proxy.unlink("foo").await.expect("FIDL call failed"))
            .expect("unlink failed");

        // The child should immediately appear unlinked...
        assert_eq!(
            open_file_validating(&dir_proxy, OPEN_RIGHT_READABLE, MODE_TYPE_FILE, "foo")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );

        // But its contents should still be readable from the other handle.
        file_proxy.seek(0, SeekOrigin::Start).await.expect("seek failed");
        let rbuf = read_file_bytes(&file_proxy).await.expect("read failed");
        assert_eq!(rbuf, buf);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unlink_dir_with_children_fails() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_volume = root_volume(&filesystem).await?;
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await?).await;
        let dir = vol.root().clone();

        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let subdir_proxy = open_dir_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await
        .expect("Create directory failed");
        open_file_validating(
            &subdir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE,
            MODE_TYPE_FILE,
            "bar",
        )
        .await
        .expect("Create file failed");

        assert_eq!(
            Status::from_raw(dir_proxy.unlink("foo").await.expect("FIDL call failed")),
            Status::NOT_EMPTY
        );

        Status::ok(subdir_proxy.unlink("bar").await.expect("FIDL call failed"))
            .expect("unlink failed");
        Status::ok(dir_proxy.unlink("foo").await.expect("FIDL call failed"))
            .expect("unlink failed");

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_unlink_dir_makes_directory_immutable() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_volume = root_volume(&filesystem).await?;
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await?).await;
        let dir = vol.root().clone();

        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        let subdir_proxy = open_dir_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await
        .expect("Create directory failed");

        Status::ok(dir_proxy.unlink("foo").await.expect("FIDL call failed"))
            .expect("unlink failed");

        assert_eq!(
            open_file_validating(
                &subdir_proxy,
                OPEN_RIGHT_READABLE | OPEN_FLAG_CREATE,
                MODE_TYPE_FILE,
                "bar"
            )
            .await
            .expect_err("Create file succeeded")
            .root_cause()
            .downcast_ref::<Status>()
            .expect("No status"),
            &Status::ACCESS_DENIED,
        );

        Ok(())
    }

    #[fasync::run(10, test)]
    async fn test_unlink_directory_with_children_race() -> Result<(), Error> {
        let device = Arc::new(FakeDevice::new(2048, 512));
        let filesystem = FxFilesystem::new_empty(device.clone()).await?;
        let root_volume = root_volume(&filesystem).await?;
        let vol = FxVolumeAndRoot::new(root_volume.new_volume("vol").await?).await;
        let dir = vol.root().clone();

        let (dir_proxy, dir_server_end) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("Create proxy to succeed");

        dir.open(
            ExecutionScope::new(),
            OPEN_FLAG_DIRECTORY | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            Path::empty(),
            ServerEnd::new(dir_server_end.into_channel()),
        );

        const PARENT: &str = "foo";
        const CHILD: &str = "bar";
        const GRANDCHILD: &str = "baz";
        open_dir_validating(
            &dir_proxy,
            OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
            MODE_TYPE_DIRECTORY,
            PARENT,
        )
        .await
        .expect("Create dir failed");

        let open_parent = || async {
            open_dir_validating(
                &dir_proxy,
                OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_DIRECTORY,
                PARENT,
            )
            .await
            .expect("open dir failed")
        };
        let parent = open_parent().await;

        // Each iteration proceeds as follows:
        //  - Initialize a directory foo/bar/. (This might still be around from the previous
        //    iteration, which is fine.)
        //  - In one thread, try to unlink foo/bar/.
        //  - In another thread, try to add a file foo/bar/baz.
        for _ in 0..100 {
            open_dir_validating(
                &parent,
                OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                MODE_TYPE_DIRECTORY,
                CHILD,
            )
            .await
            .expect("Create dir failed");

            let parent = open_parent().await;
            let deleter = fasync::Task::spawn(async move {
                let wait_time = rand::thread_rng().gen_range(0, 5);
                fasync::Timer::new(Duration::from_millis(wait_time)).await;
                match Status::from_raw(parent.unlink(CHILD).await.expect("FIDL call failed")) {
                    Status::OK => {}
                    Status::NOT_EMPTY => {}
                    s => panic!("Unexpected status from unlink: {:?}", s),
                };
            });

            let parent = open_parent().await;
            let writer = fasync::Task::spawn(async move {
                let child_or = open_dir_validating(
                    &parent,
                    OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                    MODE_TYPE_DIRECTORY,
                    CHILD,
                )
                .await;
                if let Err(e) = &child_or {
                    // The directory was already deleted.
                    assert_eq!(
                        e.root_cause().downcast_ref::<Status>().expect("No status"),
                        &Status::NOT_FOUND
                    );
                    return;
                }
                let child = child_or.unwrap();
                match open_file_validating(
                    &child,
                    OPEN_FLAG_CREATE | OPEN_RIGHT_READABLE,
                    MODE_TYPE_FILE,
                    GRANDCHILD,
                )
                .await
                {
                    Ok(grandchild) => {
                        // We added the child before the directory was deleted; go ahead and
                        // clean up.
                        grandchild.close().await.expect("close failed");
                        Status::ok(child.unlink(GRANDCHILD).await.expect("FIDL call failed"))
                            .expect("unlink failed");
                    }
                    Err(e) => {
                        // The directory started to be deleted before we created a child.
                        // Make sure we get the right error.
                        assert_eq!(
                            e.root_cause().downcast_ref::<Status>().expect("No status"),
                            &Status::ACCESS_DENIED,
                        );
                    }
                };
            });
            writer.await;
            deleter.await;
        }

        Ok(())
    }
}
