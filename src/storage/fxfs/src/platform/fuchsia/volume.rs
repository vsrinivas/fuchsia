// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        errors::FxfsError,
        filesystem::{self, SyncOptions},
        log::*,
        object_store::{
            directory::{Directory, ObjectDescriptor},
            transaction::Options,
            HandleOptions, HandleOwner, ObjectStore,
        },
        platform::fuchsia::{
            directory::FxDirectory,
            file::FxFile,
            node::{FxNode, GetResult, NodeCache},
            pager::{Pager, PagerExecutor},
            vmo_data_buffer::VmoDataBuffer,
        },
        serialized_types::LATEST_VERSION,
    },
    anyhow::{bail, Error},
    async_trait::async_trait,
    fidl_fuchsia_io as fio,
    fs_inspect::{FsInspect, InfoData, UsageData, VolumeData},
    fuchsia_async as fasync,
    futures::{self, channel::oneshot, FutureExt},
    std::{
        convert::TryInto,
        sync::{Arc, Mutex},
        time::Duration,
    },
    vfs::execution_scope::ExecutionScope,
};

pub const DEFAULT_FLUSH_PERIOD: Duration = Duration::from_secs(20);
const FXFS_INFO_NAME: &'static str = "fxfs";

/// FxVolume represents an opened volume. It is also a (weak) cache for all opened Nodes within the
/// volume.
pub struct FxVolume {
    cache: NodeCache,
    store: Arc<ObjectStore>,
    pager: Pager<FxFile>,
    executor: fasync::EHandle,

    // A tuple of the actual task and a channel to signal to terminate the task.
    flush_task: Mutex<Option<(fasync::Task<()>, oneshot::Sender<()>)>>,

    // Unique identifier of the filesystem that owns this volume.
    fs_id: u64,

    // The execution scope for this volume.
    scope: ExecutionScope,
}

impl FxVolume {
    fn new(store: Arc<ObjectStore>, fs_id: u64) -> Result<Self, Error> {
        Ok(Self {
            cache: NodeCache::new(),
            store,
            pager: Pager::<FxFile>::new(PagerExecutor::global_instance())?,
            executor: fasync::EHandle::local(),
            flush_task: Mutex::new(None),
            fs_id,
            scope: ExecutionScope::new(),
        })
    }

    pub fn store(&self) -> &Arc<ObjectStore> {
        &self.store
    }

    pub fn cache(&self) -> &NodeCache {
        &self.cache
    }

    pub fn pager(&self) -> &Pager<FxFile> {
        &self.pager
    }

    pub fn executor(&self) -> &fasync::EHandle {
        &self.executor
    }

    pub fn id(&self) -> u64 {
        self.fs_id
    }

    pub fn scope(&self) -> &ExecutionScope {
        &self.scope
    }

    pub async fn terminate(&self) {
        self.scope.shutdown();
        self.scope.wait().await;
        self.pager.terminate().await;
        self.store.filesystem().graveyard().flush().await;
        let task = std::mem::replace(&mut *self.flush_task.lock().unwrap(), None);
        if let Some((task, terminate)) = task {
            let _ = terminate.send(());
            task.await;
        }
        self.flush_all_files().await;
        if self.store.crypt().is_some() {
            if let Err(e) = self.store.lock().await {
                // The store will be left in a safe state and there won't be data-loss unless
                // there's an issue flushing the journal later.
                warn!(error = e.as_value(), "Locking store error");
            }
        }
        let sync_status = self
            .store
            .filesystem()
            .sync(SyncOptions { flush_device: true, ..Default::default() })
            .await;
        if let Err(e) = sync_status {
            error!(error = e.as_value(), "Failed to sync filesystem; data may be lost");
        }
    }

    /// Attempts to get a node from the node cache. If the node wasn't present in the cache, loads
    /// the object from the object store, installing the returned node into the cache and returns
    /// the newly created FxNode backed by the loaded object.  |parent| is only set on the node if
    /// the node was not present in the cache.  Otherwise, it is ignored.
    pub async fn get_or_load_node(
        self: &Arc<Self>,
        object_id: u64,
        object_descriptor: ObjectDescriptor,
        parent: Option<Arc<FxDirectory>>,
    ) -> Result<Arc<dyn FxNode>, Error> {
        match self.cache.get_or_reserve(object_id).await {
            GetResult::Node(node) => Ok(node),
            GetResult::Placeholder(placeholder) => {
                let node = match object_descriptor {
                    ObjectDescriptor::File => FxFile::new(
                        ObjectStore::open_object(
                            self,
                            object_id,
                            HandleOptions::default(),
                            self.store().crypt().as_deref(),
                        )
                        .await?,
                    ) as Arc<dyn FxNode>,
                    ObjectDescriptor::Directory => {
                        Arc::new(FxDirectory::new(parent, Directory::open(self, object_id).await?))
                            as Arc<dyn FxNode>
                    }
                    _ => bail!(FxfsError::Inconsistent),
                };
                placeholder.commit(&node);
                Ok(node)
            }
        }
    }

    pub fn into_store(self) -> Arc<ObjectStore> {
        self.store
    }

    /// Marks the given directory deleted.
    pub fn mark_directory_deleted(&self, object_id: u64) {
        if let Some(node) = self.cache.get(object_id) {
            // It's possible that node is a placeholder, in which case we don't need to wait for it
            // to be resolved because it should be blocked behind the locks that are held by the
            // caller, and once they're dropped, it'll be found to be deleted via the tree.
            if let Ok(dir) = node.into_any().downcast::<FxDirectory>() {
                dir.set_deleted();
            }
        }
    }

    /// Removes resources associated with |object_id| (which ought to be a file), if there are no
    /// open connections to that file.
    ///
    /// This must be called *after committing* a transaction which deletes the last reference to
    /// |object_id|, since before that point, new connections could be established.
    pub(super) async fn maybe_purge_file(&self, object_id: u64) -> Result<(), Error> {
        if let Some(node) = self.cache.get(object_id) {
            if let Ok(file) = node.into_any().downcast::<FxFile>() {
                if !file.mark_purged() {
                    return Ok(());
                }
            }
        }
        // If this fails, the graveyard should clean it up on next mount.
        self.store
            .tombstone(object_id, Options { borrow_metadata_space: true, ..Default::default() })
            .await?;
        Ok(())
    }

    /// Starts the background flush task.  This task will periodically scan all files and flush them
    /// to disk.
    /// The task will hold a strong reference to the FxVolume while it is running, so the task must
    /// be closed later with Self::terminate, or the FxVolume will never be dropped.
    pub fn start_flush_task(self: &Arc<Self>, period: Duration) {
        let mut flush_task = self.flush_task.lock().unwrap();
        if flush_task.is_none() {
            let (tx, rx) = oneshot::channel();
            *flush_task = Some((fasync::Task::spawn(self.clone().flush_task(period, rx)), tx));
        }
    }

    async fn flush_task(self: Arc<Self>, period: Duration, terminate: oneshot::Receiver<()>) {
        debug!(store_id = self.store.store_object_id(), "FxVolume::flush_task start");
        let mut terminate = terminate.fuse();
        loop {
            if futures::select!(
                _ = fasync::Timer::new(period).fuse() => false,
                _ = terminate => true,
            ) {
                break;
            }
            self.flush_all_files().await;
        }
        debug!(store_id = self.store.store_object_id(), "FxVolume::flush_task end");
    }

    async fn flush_all_files(&self) {
        let mut flushed = 0;
        for file in self.cache.files() {
            if let Err(e) = file.flush().await {
                warn!(
                    store_id = self.store.store_object_id(),
                    oid = file.object_id(),
                    error = e.as_value(),
                    "Failed to flush",
                )
            }
            flushed += 1;
        }
        debug!(store_id = self.store.store_object_id(), file_count = flushed, "FxVolume flushed");
    }
}

impl HandleOwner for FxVolume {
    type Buffer = VmoDataBuffer;

    fn create_data_buffer(&self, object_id: u64, initial_size: u64) -> Self::Buffer {
        // TODO(fxbug.dev/102659) Enable trap dirty.
        self.pager
            .create_vmo(object_id, initial_size, /*enable_trap_dirty=*/ false)
            .unwrap()
            .try_into()
            .unwrap()
    }
}

impl AsRef<ObjectStore> for FxVolume {
    fn as_ref(&self) -> &ObjectStore {
        &self.store
    }
}

#[async_trait]
impl FsInspect for FxVolume {
    fn get_info_data(&self) -> InfoData {
        InfoData {
            id: self.fs_id,
            fs_type: fidl_fuchsia_fs::VfsType::Fxfs.into_primitive().into(),
            name: FXFS_INFO_NAME.into(),
            version_major: LATEST_VERSION.major.into(),
            version_minor: LATEST_VERSION.minor.into(),
            block_size: self.store.block_size(),
            max_filename_length: fio::MAX_FILENAME,
            // TODO(fxbug.dev/93770): Determine how to report oldest on-disk version if required.
            oldest_version: None,
        }
    }

    async fn get_usage_data(&self) -> UsageData {
        let info = self.store.filesystem().get_info();
        let object_count = self.store().object_count();
        UsageData {
            total_bytes: info.total_bytes,
            used_bytes: info.used_bytes,
            total_nodes: TOTAL_NODES,
            used_nodes: object_count,
        }
    }

    fn get_volume_data(&self) -> VolumeData {
        // Since we're not using FVM these values should all be set to zero.
        VolumeData {
            size_bytes: 0,
            size_limit_bytes: 0,
            available_space_bytes: 0,
            // TODO(fxbug.dev/93770): Handle out of space events.
            out_of_space_events: 0,
        }
    }
}

#[derive(Clone)]
pub struct FxVolumeAndRoot {
    volume: Arc<FxVolume>,
    root: Arc<FxDirectory>,
}

impl FxVolumeAndRoot {
    pub async fn new(store: Arc<ObjectStore>, unique_id: u64) -> Result<Self, Error> {
        let volume = Arc::new(FxVolume::new(store, unique_id)?);
        let root_object_id = volume.store().root_directory_object_id();
        let root_dir = Directory::open(&volume, root_object_id).await?;
        let root = Arc::new(FxDirectory::new(None, root_dir));
        match volume.cache.get_or_reserve(root_object_id).await {
            GetResult::Node(_) => unreachable!(),
            GetResult::Placeholder(placeholder) => {
                placeholder.commit(&(root.clone() as Arc<dyn FxNode>))
            }
        }
        Ok(Self { volume, root })
    }

    pub fn volume(&self) -> &Arc<FxVolume> {
        &self.volume
    }

    pub fn root(&self) -> &Arc<FxDirectory> {
        &self.root
    }

    #[cfg(test)]
    pub(super) fn into_volume(self) -> Arc<FxVolume> {
        self.volume
    }
}

// The correct number here is arguably u64::MAX - 1 (because node 0 is reserved). There's a bug
// where inspect test cases fail if we try and use that, possibly because of a signed/unsigned bug.
// See fxbug.dev/87152.  Until that's fixed, we'll have to use i64::MAX.
const TOTAL_NODES: u64 = i64::MAX as u64;

// An array used to initialize the FilesystemInfo |name| field. This just spells "fxfs" 0-padded to
// 32 bytes.
const FXFS_INFO_NAME_FIDL: [i8; 32] = [
    0x66, 0x78, 0x66, 0x73, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    0, 0, 0, 0,
];

pub fn info_to_filesystem_info(
    info: filesystem::Info,
    block_size: u64,
    object_count: u64,
    fs_id: u64,
) -> fio::FilesystemInfo {
    fio::FilesystemInfo {
        total_bytes: info.total_bytes,
        used_bytes: info.used_bytes,
        total_nodes: TOTAL_NODES,
        used_nodes: object_count,
        // TODO(fxbug.dev/93770): Support free_shared_pool_bytes.
        free_shared_pool_bytes: 0,
        fs_id,
        block_size: block_size as u32,
        max_filename_size: fio::MAX_FILENAME as u32,
        fs_type: fidl_fuchsia_fs::VfsType::Fxfs.into_primitive(),
        padding: 0,
        name: FXFS_INFO_NAME_FIDL,
    }
}

#[cfg(test)]
mod tests {
    use {
        crate::{
            crypt::insecure::InsecureCrypt,
            filesystem::FxFilesystem,
            object_handle::{ObjectHandle, ObjectHandleExt},
            object_store::{
                directory::ObjectDescriptor,
                transaction::{Options, TransactionHandler},
                volume::root_volume,
                HandleOptions, ObjectStore,
            },
            platform::fuchsia::{
                file::FxFile,
                testing::{
                    close_dir_checked, close_file_checked, open_dir, open_dir_checked, open_file,
                    open_file_checked, TestFixture,
                },
                volume::FxVolumeAndRoot,
            },
        },
        fidl_fuchsia_io as fio, fuchsia_async as fasync,
        fuchsia_fs::file,
        fuchsia_zircon::Status,
        std::sync::Arc,
        storage_device::{fake_device::FakeDevice, DeviceHolder},
        vfs::file::FileIo,
    };

    #[fasync::run(10, test)]
    async fn test_rename_different_dirs() {
        use fuchsia_zircon::Event;

        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let src = open_dir_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await;

        let dst = open_dir_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::DIRECTORY,
            fio::MODE_TYPE_DIRECTORY,
            "bar",
        )
        .await;

        let f =
            open_file_checked(&root, fio::OpenFlags::CREATE, fio::MODE_TYPE_FILE, "foo/a").await;
        close_file_checked(f).await;

        let (status, dst_token) = dst.get_token().await.expect("FIDL call failed");
        Status::ok(status).expect("get_token failed");
        src.rename("a", Event::from(dst_token.unwrap()), "b")
            .await
            .expect("FIDL call failed")
            .expect("rename failed");

        assert_eq!(
            open_file(&root, fio::OpenFlags::empty(), fio::MODE_TYPE_FILE, "foo/a")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );
        let f =
            open_file_checked(&root, fio::OpenFlags::empty(), fio::MODE_TYPE_FILE, "bar/b").await;
        close_file_checked(f).await;

        close_dir_checked(dst).await;
        close_dir_checked(src).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
    async fn test_rename_same_dir() {
        use fuchsia_zircon::Event;
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let src = open_dir_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await;

        let f =
            open_file_checked(&root, fio::OpenFlags::CREATE, fio::MODE_TYPE_FILE, "foo/a").await;
        close_file_checked(f).await;

        let (status, src_token) = src.get_token().await.expect("FIDL call failed");
        Status::ok(status).expect("get_token failed");
        src.rename("a", Event::from(src_token.unwrap()), "b")
            .await
            .expect("FIDL call failed")
            .expect("rename failed");

        assert_eq!(
            open_file(&root, fio::OpenFlags::empty(), fio::MODE_TYPE_FILE, "foo/a")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );
        let f =
            open_file_checked(&root, fio::OpenFlags::empty(), fio::MODE_TYPE_FILE, "foo/b").await;
        close_file_checked(f).await;

        close_dir_checked(src).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
    async fn test_rename_overwrites_file() {
        use fuchsia_zircon::Event;
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let src = open_dir_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await;

        let dst = open_dir_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::DIRECTORY,
            fio::MODE_TYPE_DIRECTORY,
            "bar",
        )
        .await;

        // The src file is non-empty.
        let src_file = open_file_checked(
            &root,
            fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_FILE,
            "foo/a",
        )
        .await;
        let buf = vec![0xaa as u8; 8192];
        file::write(&src_file, buf.as_slice()).await.expect("Failed to write to file");
        close_file_checked(src_file).await;

        // The dst file is empty (so we can distinguish it).
        let f =
            open_file_checked(&root, fio::OpenFlags::CREATE, fio::MODE_TYPE_FILE, "bar/b").await;
        close_file_checked(f).await;

        let (status, dst_token) = dst.get_token().await.expect("FIDL call failed");
        Status::ok(status).expect("get_token failed");
        src.rename("a", Event::from(dst_token.unwrap()), "b")
            .await
            .expect("FIDL call failed")
            .expect("rename failed");

        assert_eq!(
            open_file(&root, fio::OpenFlags::empty(), fio::MODE_TYPE_FILE, "foo/a")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );
        let file =
            open_file_checked(&root, fio::OpenFlags::RIGHT_READABLE, fio::MODE_TYPE_FILE, "bar/b")
                .await;
        let buf = file::read(&file).await.expect("read file failed");
        assert_eq!(buf, vec![0xaa as u8; 8192]);
        close_file_checked(file).await;

        close_dir_checked(dst).await;
        close_dir_checked(src).await;
        fixture.close().await;
    }

    #[fasync::run(10, test)]
    async fn test_rename_overwrites_dir() {
        use fuchsia_zircon::Event;
        let fixture = TestFixture::new().await;
        let root = fixture.root();

        let src = open_dir_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            "foo",
        )
        .await;

        let dst = open_dir_checked(
            &root,
            fio::OpenFlags::CREATE
                | fio::OpenFlags::RIGHT_READABLE
                | fio::OpenFlags::RIGHT_WRITABLE
                | fio::OpenFlags::DIRECTORY,
            fio::MODE_TYPE_DIRECTORY,
            "bar",
        )
        .await;

        // The src dir is non-empty.
        open_dir_checked(
            &root,
            fio::OpenFlags::CREATE | fio::OpenFlags::RIGHT_WRITABLE,
            fio::MODE_TYPE_DIRECTORY,
            "foo/a",
        )
        .await;
        open_file_checked(&root, fio::OpenFlags::CREATE, fio::MODE_TYPE_FILE, "foo/a/file").await;
        open_dir_checked(&root, fio::OpenFlags::CREATE, fio::MODE_TYPE_DIRECTORY, "bar/b").await;

        let (status, dst_token) = dst.get_token().await.expect("FIDL call failed");
        Status::ok(status).expect("get_token failed");
        src.rename("a", Event::from(dst_token.unwrap()), "b")
            .await
            .expect("FIDL call failed")
            .expect("rename failed");

        assert_eq!(
            open_dir(&root, fio::OpenFlags::empty(), fio::MODE_TYPE_DIRECTORY, "foo/a")
                .await
                .expect_err("Open succeeded")
                .root_cause()
                .downcast_ref::<Status>()
                .expect("No status"),
            &Status::NOT_FOUND,
        );
        let f =
            open_file_checked(&root, fio::OpenFlags::empty(), fio::MODE_TYPE_FILE, "bar/b/file")
                .await;
        close_file_checked(f).await;

        close_dir_checked(dst).await;
        close_dir_checked(src).await;

        fixture.close().await;
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_background_flush() {
        // We have to do a bit of set-up ourselves for this test, since we want to be able to access
        // the underlying StoreObjectHandle at the same time as the FxFile which corresponds to it.
        let device = DeviceHolder::new(FakeDevice::new(8192, 512));
        let filesystem = FxFilesystem::new_empty(device).await.unwrap();
        {
            let root_volume = root_volume(filesystem.clone()).await.unwrap();
            let volume =
                root_volume.new_volume("vol", Some(Arc::new(InsecureCrypt::new()))).await.unwrap();
            let mut transaction = filesystem
                .clone()
                .new_transaction(&[], Options::default())
                .await
                .expect("new_transaction failed");
            let object_id = ObjectStore::create_object(
                &volume,
                &mut transaction,
                HandleOptions::default(),
                None,
            )
            .await
            .expect("create_object failed")
            .object_id();
            transaction.commit().await.expect("commit failed");
            let vol = FxVolumeAndRoot::new(volume.clone(), 0).await.unwrap();

            let file = vol
                .volume()
                .get_or_load_node(object_id, ObjectDescriptor::File, None)
                .await
                .expect("get_or_load_node failed")
                .into_any()
                .downcast::<FxFile>()
                .expect("Not a file");

            // Write some data to the file, which will only go to the cache for now.
            file.write_at(0, &[123u8]).await.expect("write_at failed");

            let data_has_persisted = || async {
                // We have to reopen the object each time since this is a distinct handle from the
                // one managed by the FxFile.
                let object =
                    ObjectStore::open_object(&volume, object_id, HandleOptions::default(), None)
                        .await
                        .expect("open_object failed");
                let data = object.contents(8192).await.expect("read failed");
                data.len() == 1 && data[..] == [123u8]
            };
            assert!(!data_has_persisted().await);

            vol.volume().start_flush_task(std::time::Duration::from_millis(100));

            let mut wait = 100;
            loop {
                if data_has_persisted().await {
                    break;
                }
                fasync::Timer::new(std::time::Duration::from_millis(wait)).await;
                wait *= 2;
            }

            vol.volume().terminate().await;
        }

        filesystem.close().await.expect("close filesystem failed");
        let device = filesystem.take_device().await;
        device.ensure_unique();
    }

    #[fasync::run(10, test)]
    async fn test_unencrypted_volume() {
        let fixture = TestFixture::new_unencrypted().await;
        let root = fixture.root();

        let f = open_file_checked(&root, fio::OpenFlags::CREATE, fio::MODE_TYPE_FILE, "foo").await;
        close_file_checked(f).await;

        fixture.close().await;
    }
}
