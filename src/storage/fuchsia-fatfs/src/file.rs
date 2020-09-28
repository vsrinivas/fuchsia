// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        directory::FatDirectory,
        filesystem::{FatFilesystem, FatFilesystemInner},
        node::Node,
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
        pin::Pin,
        sync::{Arc, RwLock},
    },
    vfs::{
        directory::entry::{DirectoryEntry, EntryInfo},
        execution_scope::ExecutionScope,
        file::{connection, File as VfsFile, SharingMode},
        path::Path,
    },
};

fn extend(file: &mut File<'_>, mut current: u64, target: u64) -> Result<(), Status> {
    let zeros = vec![0; 8192];
    while current < target {
        let to_do = (std::cmp::min(target, (current + 8192) / 8192 * 8192) - current) as usize;
        let written = file.write(&zeros[..to_do]).map_err(fatfs_error_to_status)? as u64;
        if written == 0 {
            return Err(Status::NO_SPACE);
        }
        current += written;
    }
    Ok(())
}

fn seek_for_write(file: &mut File<'_>, offset: u64) -> Result<(), Status> {
    if offset > fatfs::MAX_FILE_SIZE as u64 {
        return Err(Status::INVALID_ARGS);
    }
    let real_offset = file.seek(std::io::SeekFrom::Start(offset)).map_err(fatfs_error_to_status)?;
    if real_offset == offset {
        return Ok(());
    }
    assert!(real_offset < offset);
    let result = extend(file, real_offset, offset);
    if let Err(e) = result {
        // Return the file to its original size.
        file.seek(std::io::SeekFrom::Start(real_offset)).map_err(fatfs_error_to_status)?;
        file.truncate().map_err(fatfs_error_to_status)?;
        return Err(e);
    }
    Ok(())
}

struct FatFileData {
    name: String,
    parent: Option<Arc<FatDirectory>>,
}

/// Represents a single file on the disk.
pub struct FatFile {
    file: UnsafeCell<FatfsFileRef>,
    filesystem: Pin<Arc<FatFilesystem>>,
    data: RwLock<FatFileData>,
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
        filesystem: Pin<Arc<FatFilesystem>>,
        name: String,
    ) -> Arc<Self> {
        Arc::new(FatFile {
            file: UnsafeCell::new(file),
            filesystem,
            data: RwLock::new(FatFileData { parent: Some(parent), name }),
        })
    }

    /// Borrow the underlying Fatfs File mutably.
    pub(crate) fn borrow_file_mut<'a>(&self, fs: &'a FatFilesystemInner) -> Option<&mut File<'a>> {
        // Safe because the file is protected by the lock on fs.
        unsafe { self.file.get().as_mut() }.unwrap().borrow_mut(fs)
    }

    pub fn borrow_file<'a>(&self, fs: &'a FatFilesystemInner) -> Result<&File<'a>, Status> {
        // Safe because the file is protected by the lock on fs.
        unsafe { self.file.get().as_ref() }.unwrap().borrow(fs).ok_or(Status::BAD_HANDLE)
    }

    async fn write_or_append(
        &self,
        offset: Option<u64>,
        content: &[u8],
    ) -> Result<(u64, u64), Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let file = self.borrow_file_mut(&fs_lock).ok_or(Status::BAD_HANDLE)?;
        let mut file_offset = match offset {
            Some(offset) => {
                seek_for_write(file, offset)?;
                offset
            }
            None => file.seek(std::io::SeekFrom::End(0)).map_err(fatfs_error_to_status)?,
        };
        let mut total_written = 0;
        while total_written < content.len() {
            let written = file.write(&content[total_written..]).map_err(fatfs_error_to_status)?;
            if written == 0 {
                break;
            }
            total_written += written;
            file_offset += written as u64;
            let result = file.write(&content[total_written..]).map_err(fatfs_error_to_status);
            match result {
                Ok(0) => break,
                Ok(written) => {
                    total_written += written;
                    file_offset += written as u64;
                }
                Err(e) => {
                    if total_written > 0 {
                        break;
                    }
                    return Err(e);
                }
            }
        }
        self.filesystem.mark_dirty();
        Ok((total_written as u64, file_offset))
    }
}

impl Node for FatFile {
    /// Flush to disk and invalidate the reference that's contained within this FatFile.
    /// Any operations on the file will return Status::BAD_HANDLE until it is re-attached.
    fn detach(&self, fs: &FatFilesystemInner) {
        // Safe because we hold the fs lock.
        let file = unsafe { self.file.get().as_mut() }.unwrap();
        // This causes a flush to disk when the underlying fatfs File is dropped.
        file.take(fs);
    }

    /// Attach to the given parent and re-open the underlying `FatfsFileRef` this file represents.
    fn attach(
        &self,
        new_parent: Arc<FatDirectory>,
        name: &str,
        fs: &FatFilesystemInner,
    ) -> Result<(), Status> {
        let mut data = self.data.write().unwrap();
        data.name = name.to_owned();
        // Safe because we hold the fs lock.
        let file = unsafe { self.file.get().as_mut() }.unwrap();
        // Safe because we have a reference to the FatFilesystem.
        unsafe { file.maybe_reopen(fs, &new_parent, name)? };
        data.parent.replace(new_parent);
        Ok(())
    }

    fn did_delete(&self) {
        self.data.write().unwrap().parent.take();
    }

    fn open_ref(&self, fs_lock: &FatFilesystemInner) -> Result<(), Status> {
        let data = self.data.read().unwrap();
        let file_ref = unsafe { self.file.get().as_mut() }.unwrap();
        unsafe { file_ref.open(&fs_lock, data.parent.as_deref(), &data.name) }
    }

    /// Close the underlying FatfsFileRef, regardless of the number of open connections.
    fn shut_down(&self, fs: &FatFilesystemInner) -> Result<(), Status> {
        unsafe { self.file.get().as_mut() }.unwrap().take(fs);
        Ok(())
    }

    fn flush_dir_entry(&self, fs: &FatFilesystemInner) -> Result<(), Status> {
        if let Some(file) = self.borrow_file_mut(fs) {
            file.flush_dir_entry().map_err(fatfs_error_to_status)?
        }
        Ok(())
    }

    fn close_ref(&self, fs: &FatFilesystemInner) {
        unsafe { self.file.get().as_mut() }.unwrap().close(fs);
    }
}

impl Debug for FatFile {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("FatFile").field("name", &self.data.read().unwrap().name).finish()
    }
}

#[async_trait]
impl VfsFile for FatFile {
    async fn open(&self, _flags: u32) -> Result<(), Status> {
        Ok(())
    }

    async fn read_at(&self, offset: u64, count: u64) -> Result<Vec<u8>, Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let file = self.borrow_file_mut(&fs_lock).ok_or(Status::BAD_HANDLE)?;

        let real_offset =
            file.seek(std::io::SeekFrom::Start(offset)).map_err(fatfs_error_to_status)?;
        // Technically, we don't need to do this because the read should return zero bytes later,
        // but it's better to be explicit.
        if real_offset != offset {
            return Ok(Vec::new());
        }
        let mut result = Vec::with_capacity(count as usize);
        result.resize(count as usize, 0);
        let mut total_read = 0;
        while total_read < count as usize {
            let read = file.read(&mut result[total_read..]).map_err(fatfs_error_to_status)?;
            if read == 0 {
                break;
            }
            total_read += read;
        }
        result.truncate(total_read);
        Ok(result)
    }

    async fn write_at(&self, offset: u64, content: &[u8]) -> Result<u64, Status> {
        self.write_or_append(Some(offset), content).await.map(|r| r.0)
    }

    async fn append(&self, content: &[u8]) -> Result<(u64, u64), Status> {
        self.write_or_append(None, content).await
    }

    async fn truncate(&self, length: u64) -> Result<(), Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let file = self.borrow_file_mut(&fs_lock).ok_or(Status::BAD_HANDLE)?;
        seek_for_write(file, length)?;
        file.truncate().map_err(fatfs_error_to_status)?;
        self.filesystem.mark_dirty();
        Ok(())
    }

    async fn get_buffer(&self, _mode: SharingMode, _flags: u32) -> Result<Option<Buffer>, Status> {
        // Not supported, so return None.
        Ok(None)
    }

    async fn get_attrs(&self) -> Result<NodeAttributes, Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let file = self.borrow_file(&fs_lock)?;
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
        let file = self.borrow_file_mut(&fs_lock).ok_or(Status::BAD_HANDLE)?;

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
            self.filesystem.mark_dirty();
        }
        Ok(())
    }

    async fn get_size(&self) -> Result<u64, Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let file = self.borrow_file(&fs_lock)?;
        Ok(file.len() as u64)
    }

    async fn close(&self) -> Result<(), Status> {
        self.close_ref(&self.filesystem.lock().unwrap());
        Ok(())
    }

    async fn sync(&self) -> Result<(), Status> {
        let fs_lock = self.filesystem.lock().unwrap();
        let file = self.borrow_file_mut(&fs_lock).ok_or(Status::BAD_HANDLE)?;

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
        let status = if !path.is_empty() {
            Err(Status::NOT_DIR)
        } else {
            self.open_ref(&self.filesystem.lock().unwrap())
        };
        match status {
            Ok(_) => {}
            Err(e) => {
                server_end
                    .close_with_epitaph(e)
                    .unwrap_or_else(|e| fx_log_err!("Failing failed: {:?}", e));
                return;
            }
        }
        connection::io1::FileConnection::<FatFile>::create_connection(
            // Note readable/writable do not override what's set in flags, they merely tell the
            // FileConnection that it's valid to open the file readable/writable.
            scope.clone(),
            connection::util::OpenFile::new(self, scope),
            flags,
            mode,
            server_end,
            /*readable=*/ true,
            /*writable=*/ true,
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
            node::{Closer, FatNode},
            tests::{TestDiskContents, TestFatDisk},
        },
        fuchsia_async as fasync,
    };

    const TEST_DISK_SIZE: u64 = 2048 << 10; // 2048K
    const TEST_FILE_CONTENT: &str = "test file contents";

    struct TestFile(Arc<FatFile>);

    impl TestFile {
        fn new() -> Self {
            let disk = TestFatDisk::empty_disk(TEST_DISK_SIZE);
            let structure =
                TestDiskContents::dir().add_child("test_file", TEST_FILE_CONTENT.into());
            structure.create(&disk.root_dir());

            let fs = disk.into_fatfs();
            let dir = fs.get_fatfs_root();
            let mut closer = Closer::new(&fs.filesystem());
            dir.open_ref(&fs.filesystem().lock().unwrap()).expect("open_ref failed");
            closer.add(FatNode::Dir(dir.clone()));
            let file =
                match dir.open_child("test_file", 0, 0, &mut closer).expect("Open to succeed") {
                    FatNode::File(f) => f,
                    val => panic!("Unexpected value {:?}", val),
                };
            file.open_ref(&fs.filesystem().lock().unwrap()).expect("open_ref failed");
            TestFile(file)
        }
    }

    impl Drop for TestFile {
        fn drop(&mut self) {
            self.0.close_ref(&self.0.filesystem.lock().unwrap());
        }
    }

    impl std::ops::Deref for TestFile {
        type Target = Arc<FatFile>;

        fn deref(&self) -> &Self::Target {
            &self.0
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_read_at() {
        let file = TestFile::new();
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
        let file = TestFile::new();
        let attrs = file.get_attrs().await.expect("get_attrs succeeds");
        assert_eq!(attrs.mode, 0);
        assert_eq!(attrs.id, INO_UNKNOWN);
        assert_eq!(attrs.content_size, TEST_FILE_CONTENT.len() as u64);
        assert!(attrs.storage_size > TEST_FILE_CONTENT.len() as u64);
        assert_eq!(attrs.link_count, 1);
    }
}
