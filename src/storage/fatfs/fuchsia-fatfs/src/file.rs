use {
    crate::{
        directory::FatDirectory,
        filesystem::{FatFilesystem, FatFilesystemInner},
        refs::FatfsFileRef,
        types::File,
        util::{dos_to_unix_time, fatfs_error_to_status, unix_to_dos_time},
    },
    async_trait::async_trait,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_io::{self as fio, NodeAttributes, NodeMarker, INO_UNKNOWN},
    fidl_fuchsia_mem::Buffer,
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon::Status,
    std::{
        cell::UnsafeCell,
        fmt::Debug,
        io::{Read, Seek, Write},
        sync::Arc,
    },
    vfs::{
        directory::entry::{DirectoryEntry, EntryInfo},
        execution_scope::ExecutionScope,
        file::{connection, File as VfsFile, SharingMode},
        path::Path,
    },
};

/// Represents a single file on the disk.
pub struct FatFile {
    file: UnsafeCell<FatfsFileRef>,
    parent: Arc<FatDirectory>,
    filesystem: Arc<FatFilesystem>,
}

// The only member that isn't `Sync + Send` is the `file` member.
// `file` is protected by the lock on `filesystem`, so we can safely
// implement Sync + Send for FatFile.
unsafe impl Sync for FatFile {}
unsafe impl Send for FatFile {}

impl FatFile {
    /// Create a new FatFile.
    pub(crate) fn new(
        file: FatfsFileRef,
        parent: Arc<FatDirectory>,
        filesystem: Arc<FatFilesystem>,
    ) -> Arc<Self> {
        Arc::new(FatFile { file: UnsafeCell::new(file), parent, filesystem })
    }

    /// Borrow the underlying Fatfs File mutably.
    fn borrow_file_mut<'a>(&'a self, fs: &'a FatFilesystemInner) -> &'a mut File<'a> {
        // Safe because the file is protected by the lock on fs.
        unsafe { self.file.get().as_mut() }.unwrap().borrow_mut(fs)
    }

    fn borrow_file<'a>(&'a self, fs: &'a FatFilesystemInner) -> &'a File<'a> {
        // Safe because the file is protected by the lock on fs.
        unsafe { self.file.get().as_ref() }.unwrap().borrow(fs)
    }
}

impl Debug for FatFile {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("FatFile").field("parent", &self.parent).finish()
    }
}

impl Drop for FatFile {
    fn drop(&mut self) {
        // We need to drop the underlying Fatfs `File` while holding the filesystem lock,
        // to make sure that it's able to flush, etc. before getting dropped.
        let fs_lock = self.filesystem.lock().unwrap();
        // Safe because fs_lock guarantees we are the only place trying to access this file.
        unsafe { self.file.get().as_mut() }.unwrap().take(&fs_lock);
    }
}

#[async_trait]
impl VfsFile for FatFile {
    async fn open(&self, _flags: u32) -> Result<(), Status> {
        Ok(())
    }

    async fn read_at(&self, offset: u64, count: u64) -> Result<Vec<u8>, Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let file = self.borrow_file_mut(&fs_lock);

        file.seek(std::io::SeekFrom::Start(offset)).map_err(fatfs_error_to_status)?;
        let mut result = Vec::with_capacity(count as usize);
        result.resize(count as usize, 0);
        let size = file.read(&mut result).map_err(fatfs_error_to_status)?;
        result.truncate(size);
        Ok(result)
    }

    async fn write_at(&self, offset: u64, content: &[u8]) -> Result<u64, Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let file = self.borrow_file_mut(&fs_lock);

        let real_offset =
            file.seek(std::io::SeekFrom::Start(offset)).map_err(fatfs_error_to_status)?;
        if real_offset < offset {
            // TODO(fxb/56006): emulate sparse file support by padding with null bytes.
            return Err(Status::INVALID_ARGS);
        }
        let size = file.write(content).map_err(fatfs_error_to_status)?;
        Ok(size as u64)
    }

    async fn truncate(&self, length: u64) -> Result<(), Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let file = self.borrow_file_mut(&fs_lock);

        let offset = file.seek(std::io::SeekFrom::Start(length)).map_err(fatfs_error_to_status)?;
        if offset < length {
            // TODO(fxb/56006): emulate sparse file support by padding with null bytes.
            return Err(Status::INVALID_ARGS);
        }
        file.truncate().map_err(fatfs_error_to_status)?;
        Ok(())
    }

    async fn get_buffer(&self, _mode: SharingMode, _flags: u32) -> Result<Option<Buffer>, Status> {
        Ok(None)
    }

    async fn get_attrs(&self) -> Result<NodeAttributes, Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let file = self.borrow_file(&fs_lock);
        let content_size = file.len() as u64;
        let creation_time = dos_to_unix_time(file.created());
        let modification_time = dos_to_unix_time(file.modified());

        // Figure out the storage size by rounding content_size up to the nearest
        // multiple of cluster_size.
        let cluster_size = fs_lock.cluster_size() as u64;
        let storage_size = ((content_size + cluster_size - 1) / cluster_size) * cluster_size;

        Ok(NodeAttributes {
            mode: 0,
            id: INO_UNKNOWN,
            content_size,
            storage_size,
            link_count: 1,
            creation_time,
            modification_time,
        })
    }

    // Unfortunately, fatfs has deprecated the "set_created" and "set_modified" methods,
    // saying that a TimeProvider should be used instead. There doesn't seem to be a good way to
    // use a TimeProvider to change the creation/modification time of a file after the fact,
    // so we need to use the deprecated methods.
    #[allow(deprecated)]
    async fn set_attrs(&self, flags: u32, attrs: NodeAttributes) -> Result<(), Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let file = self.borrow_file_mut(&fs_lock);

        let needs_flush = flags
            & (fio::NODE_ATTRIBUTE_FLAG_CREATION_TIME | fio::NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME);

        if flags & fio::NODE_ATTRIBUTE_FLAG_CREATION_TIME != 0 {
            file.set_created(unix_to_dos_time(attrs.creation_time));
        }
        if flags & fio::NODE_ATTRIBUTE_FLAG_MODIFICATION_TIME != 0 {
            file.set_modified(unix_to_dos_time(attrs.modification_time));
        }

        if needs_flush != 0 {
            file.flush().map_err(fatfs_error_to_status)?;
        }
        Ok(())
    }

    async fn get_size(&self) -> Result<u64, Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let file = self.borrow_file(&fs_lock);
        Ok(file.len() as u64)
    }

    async fn close(&self) -> Result<(), Status> {
        Ok(())
    }

    async fn sync(&self) -> Result<(), Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let file = self.borrow_file_mut(&fs_lock);

        file.flush().map_err(fatfs_error_to_status)?;
        Ok(())
    }
}

impl DirectoryEntry for FatFile {
    fn open(
        self: Arc<Self>,
        scope: ExecutionScope,
        flags: u32,
        mode: u32,
        path: Path,
        server_end: ServerEnd<NodeMarker>,
    ) {
        if !path.is_empty() {
            server_end
                .close_with_epitaph(Status::NOT_DIR)
                .unwrap_or_else(|e| fx_log_err!("Failing failed: {:?}", e));
            return;
        }
        connection::io1::FileConnection::<FatFile>::create_connection(
            // Note readable/writable do not override what's set in flags, they merely tell the
            // FileConnection that it's valid to open the file readable/writable.
            scope, self, flags, mode, server_end, /*readable=*/ true, /*writable=*/ true,
        );
    }

    fn entry_info(&self) -> EntryInfo {
        EntryInfo::new(fio::INO_UNKNOWN, fio::DIRENT_TYPE_FILE)
    }

    fn can_hardlink(&self) -> bool {
        false
    }
}

#[cfg(test)]
mod tests {
    // We only test things here that aren't covered by fs_tests.
    use {
        super::*,
        crate::{
            directory::FatNode,
            tests::{TestDiskContents, TestFatDisk},
        },
        fuchsia_async as fasync,
    };

    const TEST_DISK_SIZE: u64 = 2048 << 10; // 2048K
    const TEST_FILE_CONTENT: &str = "test file contents";

    fn get_test_file() -> Arc<FatFile> {
        let disk = TestFatDisk::empty_disk(TEST_DISK_SIZE);
        let structure = TestDiskContents::dir().add_child("test_file", TEST_FILE_CONTENT.into());
        structure.create(&disk.root_dir());

        let fs = disk.into_fatfs();
        let dir = fs.get_fatfs_root();
        let file = match dir.open_child("test_file", 0, 0).expect("Open to succeed") {
            FatNode::File(f) => f,
            val => panic!("Unexpected value {:?}", val),
        };

        file
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_at() {
        let file = get_test_file();
        // Note: fatfs incorrectly casts u64 to i64, which causes this value to wrap
        // around and become negative, which causes seek() in read_at() to fail.
        // The error is not particularly important, because fat has a maximum 32-bit file size.
        // An error like this will only happen if an application deliberately seeks to a (very)
        // out-of-range position or reads at a nonsensical offset.
        let err = file.read_at(u64::MAX - 30, 1).await.expect_err("Read fails");
        assert_eq!(err, Status::INVALID_ARGS);
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_attrs() {
        let file = get_test_file();
        let attrs = file.get_attrs().await.expect("get_attrs succeeds");
        assert_eq!(attrs.mode, 0);
        assert_eq!(attrs.id, INO_UNKNOWN);
        assert_eq!(attrs.content_size, TEST_FILE_CONTENT.len() as u64);
        assert_eq!(attrs.storage_size, TEST_FILE_CONTENT.len() as u64);
        assert_eq!(attrs.link_count, 1);
    }
}
