// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{directory::FatDirectory, filesystem::FatFilesystem, node::Node},
    anyhow::Error,
    fatfs::FsOptions,
    fidl_fuchsia_fs::{AdminRequest, FilesystemInfo, FilesystemInfoQuery, FsType, QueryRequest},
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fuchsia_syslog::{fx_log_err, fx_log_warn},
    fuchsia_zircon::{Event, HandleBased, Rights, Status},
    std::pin::Pin,
    std::sync::Arc,
    vfs::{
        directory::{entry::DirectoryEntry, entry_container::Directory},
        execution_scope::ExecutionScope,
        path::Path,
    },
};

mod directory;
mod file;
mod filesystem;
mod node;
mod refs;
mod types;
mod util;

pub use crate::types::Disk;

/// Number of UCS-2 characters that fit in a VFAT LFN.
/// Note that FAT doesn't support the full range of Unicode characters (UCS-2 is only 16 bits),
/// and short file names can't encode the full 16-bit range of UCS-2.
/// This is the minimum possible value. For instance, a 300 byte UTF-8 string could fit inside 255
/// UCS-2 codepoints (if it had some 16 bit characters), but a 300 byte ASCII string would not fit.
const MAX_FILENAME_LEN: u32 = 255;

pub trait RootDirectory: DirectoryEntry + Directory {}
impl<T: DirectoryEntry + Directory> RootDirectory for T {}

pub struct FatFs {
    inner: Pin<Arc<FatFilesystem>>,
    root: Arc<FatDirectory>,
    fs_id: Event,
}

impl FatFs {
    /// Create a new FatFs using the given ReadWriteSeek as the disk.
    pub fn new(disk: Box<dyn Disk>) -> Result<Self, Error> {
        let (inner, root) = FatFilesystem::new(disk, FsOptions::new())?;
        Ok(FatFs { inner, root, fs_id: Event::create()? })
    }

    #[cfg(test)]
    pub fn from_filesystem(inner: Pin<Arc<FatFilesystem>>, root: Arc<FatDirectory>) -> Self {
        FatFs { inner, root, fs_id: Event::create().expect("Event::create succeeds") }
    }

    #[cfg(test)]
    pub fn get_fatfs_root(&self) -> Arc<FatDirectory> {
        self.root.clone()
    }

    #[cfg(test)]
    pub fn filesystem(&self) -> &FatFilesystem {
        return &self.inner;
    }

    pub fn is_present(&self) -> bool {
        self.inner.lock().unwrap().with_disk(|disk| disk.is_present())
    }

    /// Get the root directory of this filesystem.
    /// The caller must call close() on the returned entry when it's finished with it.
    pub fn get_root(&self) -> Result<Arc<dyn RootDirectory>, Status> {
        // Make sure it's open.
        self.root.open_ref(&self.inner.lock().unwrap())?;
        Ok(self.root.clone())
    }

    fn get_info(&self, query: FilesystemInfoQuery) -> Result<FilesystemInfo, Status> {
        let mut result = FilesystemInfo::empty();
        let fs_lock = self.inner.lock().unwrap();
        if query.contains(FilesystemInfoQuery::TotalBytes)
            || query.contains(FilesystemInfoQuery::UsedBytes)
        {
            let cluster_size = fs_lock.cluster_size() as u64;
            let total_clusters = fs_lock.total_clusters()? as u64;
            if query.contains(FilesystemInfoQuery::TotalBytes) {
                result.total_bytes = Some(cluster_size * total_clusters);
            }

            if query.contains(FilesystemInfoQuery::UsedBytes) {
                let free_clusters = fs_lock.free_clusters()? as u64;
                result.used_bytes = Some(cluster_size * (total_clusters - free_clusters));
            }
        }

        if query.contains(FilesystemInfoQuery::FsId) {
            result.fs_id = Some(self.fs_id.duplicate_handle(Rights::BASIC)?);
        }

        if query.contains(FilesystemInfoQuery::BlockSize) {
            result.block_size = Some(fs_lock.sector_size()? as u32);
        }

        if query.contains(FilesystemInfoQuery::MaxNodeNameSize) {
            result.max_node_name_size = Some(MAX_FILENAME_LEN);
        }

        if query.contains(FilesystemInfoQuery::FsType) {
            result.fs_type = Some(FsType::Fatfs);
        }

        if query.contains(FilesystemInfoQuery::Name) {
            result.name = Some("fatfs".to_owned());
        }

        Ok(result)
    }

    pub fn handle_query(&self, scope: &ExecutionScope, req: QueryRequest) -> Result<(), Error> {
        match req {
            QueryRequest::IsNodeInFilesystem { token, responder } => {
                let result = match scope.token_registry().unwrap().get_container(token.into()) {
                    Ok(Some(_)) => true,
                    _ => false,
                };
                responder.send(result)?;
            }
            QueryRequest::GetInfo { query, responder } => {
                responder.send(&mut self.get_info(query).map_err(|e| e.into_raw()))?;
            }
        };
        Ok(())
    }

    pub fn handle_admin(&self, scope: &ExecutionScope, req: AdminRequest) -> Result<(), Error> {
        match req {
            AdminRequest::Shutdown { responder } => {
                scope.shutdown();
                self.shut_down().unwrap_or_else(|e| fx_log_err!("Shutdown failed {:?}", e));
                responder.send()?;
            }
            AdminRequest::GetRoot { dir, .. } => {
                let root = match self.get_root() {
                    Ok(root) => root,
                    Err(e) => {
                        dir.close_with_epitaph(e)?;
                        return Ok(());
                    }
                };

                root.clone().open(
                    scope.clone(),
                    OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
                    0,
                    Path::empty(),
                    fidl::endpoints::ServerEnd::new(dir.into_channel()),
                );

                root.close()
                    .unwrap_or_else(|e| fx_log_warn!("Failed to close root directory: {:?}", e));
            }
        };
        Ok(())
    }

    /// Shut down the filesystem.
    pub fn shut_down(&self) -> Result<(), Status> {
        let mut fs = self.inner.lock().unwrap();
        self.root.shut_down(&fs)?;
        fs.shut_down()
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::types::{Dir, FileSystem},
        anyhow::{anyhow, Context, Error},
        fatfs::{format_volume, FormatVolumeOptions, FsOptions},
        fidl_fuchsia_io::{DirectoryProxy, FileProxy, NodeMarker, NodeProxy},
        fuchsia_async as fasync,
        fuchsia_zircon::{AsHandleRef, Status},
        futures::{future::BoxFuture, prelude::*},
        std::{collections::HashMap, io::Write, ops::Deref},
        vfs::{execution_scope::ExecutionScope, path::Path},
    };

    #[derive(Debug, PartialEq)]
    /// Helper class for creating a filesystem layout on a FAT disk programatically.
    pub enum TestDiskContents {
        File(String),
        Dir(HashMap<String, TestDiskContents>),
    }

    impl From<&str> for TestDiskContents {
        fn from(string: &str) -> Self {
            TestDiskContents::File(string.to_owned())
        }
    }

    impl TestDiskContents {
        /// Create a new, empty directory.
        pub fn dir() -> Self {
            TestDiskContents::Dir(HashMap::new())
        }

        /// Add a new child to this directory.
        pub fn add_child(mut self, name: &str, child: Self) -> Self {
            match &mut self {
                TestDiskContents::Dir(map) => map.insert(name.to_owned(), child),
                _ => panic!("Can't add to a file"),
            };
            self
        }

        /// Add this TestDiskContents to the given fatfs Dir
        pub fn create(&self, dir: &Dir<'_>) {
            match self {
                TestDiskContents::File(_) => {
                    panic!("Can't have the root directory be a file!");
                }
                TestDiskContents::Dir(map) => {
                    for (name, value) in map.iter() {
                        value.create_fs_structure(&name, dir);
                    }
                }
            };
        }

        fn create_fs_structure(&self, name: &str, dir: &Dir<'_>) {
            match self {
                TestDiskContents::File(content) => {
                    let mut file = dir.create_file(name).expect("Creating file to succeed");
                    file.truncate().expect("Truncate to succeed");
                    file.write(content.as_bytes()).expect("Write to succeed");
                }
                TestDiskContents::Dir(map) => {
                    let new_dir = dir.create_dir(name).expect("Creating directory to succeed");
                    for (name, value) in map.iter() {
                        value.create_fs_structure(&name, &new_dir);
                    }
                }
            };
        }

        pub fn verify(&self, remote: NodeProxy) -> BoxFuture<'_, Result<(), Error>> {
            // Unfortunately, there is no way to verify from the server side, so we use
            // the fuchsia.io protocol to check everything is as expected.
            match self {
                TestDiskContents::File(content) => {
                    let remote = FileProxy::new(remote.into_channel().unwrap());
                    let mut file_contents: Vec<u8> = Vec::with_capacity(content.len());

                    return async move {
                        loop {
                            let (status, mut vec) =
                                remote.read(content.len() as u64).await.context("Read failed")?;
                            let status = Status::from_raw(status);
                            if status != Status::OK {
                                // Note that we don't assert here to make the error message nicer.
                                return Err(anyhow!("Failed to read: {:?}", status));
                            }
                            if vec.len() == 0 {
                                break;
                            }
                            file_contents.append(&mut vec);
                        }

                        if file_contents.as_slice() != content.as_bytes() {
                            return Err(anyhow!(
                                "File contents mismatch: expected {}, got {}",
                                content,
                                String::from_utf8_lossy(&file_contents)
                            ));
                        }
                        Ok(())
                    }
                    .boxed();
                }
                TestDiskContents::Dir(map) => {
                    let remote = DirectoryProxy::new(remote.into_channel().unwrap());
                    // TODO(simonshields): we should check that no other files exist, but
                    // GetDirents() is going to be a pain to deal with.

                    return async move {
                        for (name, value) in map.iter() {
                            let (proxy, server_end) =
                                fidl::endpoints::create_proxy::<NodeMarker>().unwrap();
                            remote
                                .open(OPEN_RIGHT_READABLE, 0, name, server_end)
                                .context("Sending open failed")?;
                            value.verify(proxy).await.context(format!("Verifying {}", name))?;
                        }
                        Ok(())
                    }
                    .boxed();
                }
            }
        }
    }

    /// Helper class for creating an empty FAT-formatted VMO.
    pub struct TestFatDisk {
        fs: FileSystem,
    }

    impl TestFatDisk {
        /// Create an empty disk with size at least |size| bytes.
        pub fn empty_disk(size: u64) -> Self {
            let mut buffer: Vec<u8> = Vec::with_capacity(size as usize);
            buffer.resize(size as usize, 0);
            let cursor = std::io::Cursor::new(buffer.as_mut_slice());

            format_volume(cursor, FormatVolumeOptions::new()).expect("format volume to succeed");
            let wrapper: Box<dyn Disk> = Box::new(std::io::Cursor::new(buffer));
            TestFatDisk {
                fs: fatfs::FileSystem::new(wrapper, FsOptions::new())
                    .expect("creating FS to succeed"),
            }
        }

        /// Get the root directory (as a fatfs Dir).
        pub fn root_dir<'a>(&'a self) -> Dir<'a> {
            self.fs.root_dir()
        }

        /// Convert this TestFatDisk into a FatFs for testing against.
        pub fn into_fatfs(self) -> FatFs {
            let (filesystem, root_dir) = FatFilesystem::from_filesystem(self.fs);
            FatFs::from_filesystem(filesystem, root_dir)
        }
    }

    impl Deref for TestFatDisk {
        type Target = FileSystem;

        fn deref(&self) -> &Self::Target {
            &self.fs
        }
    }

    const TEST_DISK_SIZE: u64 = 2048 << 10;
    #[fasync::run_singlethreaded(test)]
    async fn test_create_disk() {
        let disk = TestFatDisk::empty_disk(TEST_DISK_SIZE);

        let structure = TestDiskContents::dir()
            .add_child("test", "This is a test file".into())
            .add_child("empty_folder", TestDiskContents::dir());

        structure.create(&disk.root_dir());

        let fatfs = disk.into_fatfs();
        let scope = ExecutionScope::new();
        let (proxy, remote) = fidl::endpoints::create_proxy::<NodeMarker>().unwrap();
        let root = fatfs.get_root().expect("get_root OK");
        root.clone().open(scope, OPEN_RIGHT_READABLE, 0, Path::empty(), remote);
        root.close().expect("Close OK");

        structure.verify(proxy).await.expect("Verify succeeds");
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_info_empty_disk() {
        let disk = TestFatDisk::empty_disk(TEST_DISK_SIZE);
        let fatfs = disk.into_fatfs();

        let result = fatfs
            .get_info(FilesystemInfoQuery::all() - FilesystemInfoQuery::FsId)
            .expect("get_info succeeds");
        assert_eq!(
            result,
            FilesystemInfo {
                // An empty FAT disk formatted by fatfs uses 46 sectors for filesystem data:
                // * 32 reserved sectors (for the BPB, etc.)
                // * 7 sectors for each of the two FATs.
                total_bytes: Some(TEST_DISK_SIZE - (46 * 512)),
                used_bytes: Some(0),
                total_nodes: None,
                used_nodes: None,
                free_shared_pool_bytes: None,
                fs_id: None,
                block_size: Some(512),
                max_node_name_size: Some(MAX_FILENAME_LEN),
                fs_type: Some(FsType::Fatfs),
                name: Some("fatfs".to_string()),
                device_path: None,
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_info_not_empty_disk() {
        let disk = TestFatDisk::empty_disk(TEST_DISK_SIZE);
        let contents = TestDiskContents::dir().add_child("file", "some text".into());
        contents.create(&disk.root_dir());
        let cluster_size = disk.cluster_size();
        let fatfs = disk.into_fatfs();

        let result = fatfs.get_info(FilesystemInfoQuery::UsedBytes).expect("get_info succeeds");
        assert_eq!(
            result,
            FilesystemInfo {
                total_bytes: None,
                // 1 cluster allocated for the file.
                used_bytes: Some(cluster_size as u64),
                total_nodes: None,
                used_nodes: None,
                free_shared_pool_bytes: None,
                fs_id: None,
                block_size: None,
                max_node_name_size: None,
                fs_type: None,
                name: None,
                device_path: None,
            }
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_get_info_fs_id() {
        let fatfs = TestFatDisk::empty_disk(TEST_DISK_SIZE).into_fatfs();
        let result = fatfs.get_info(FilesystemInfoQuery::FsId).expect("get_info succeeds");
        let result2 = fatfs.get_info(FilesystemInfoQuery::FsId).expect("get_info succeeds");
        let koid = result.fs_id.unwrap().get_koid().expect("get_koid succeeds");
        let koid2 = result2.fs_id.unwrap().get_koid().expect("get_koid succeeds");
        assert_eq!(koid, koid2);
    }
}
