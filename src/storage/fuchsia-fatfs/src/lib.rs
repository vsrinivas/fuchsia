// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::{directory::FatDirectory, filesystem::FatFilesystem, node::Node},
    anyhow::Error,
    fatfs::{FsOptions, ReadWriteSeek},
    fidl_fuchsia_fs::{AdminRequest, FilesystemInfo, FilesystemInfoQuery, FsType, QueryRequest},
    fuchsia_syslog::fx_log_err,
    fuchsia_zircon::Status,
    std::pin::Pin,
    std::sync::Arc,
    vfs::{directory::entry::DirectoryEntry, execution_scope::ExecutionScope},
};

mod directory;
mod file;
mod filesystem;
mod node;
mod refs;
mod types;
mod util;

pub struct FatFs {
    #[allow(dead_code)] // TODO(53796): implement unmount() and remove this allow.
    inner: Pin<Arc<FatFilesystem>>,
    root: Arc<FatDirectory>,
}

impl FatFs {
    /// Create a new FatFs using the given ReadWriteSeek as the disk.
    pub fn new(disk: Box<dyn ReadWriteSeek + Send>) -> Result<Self, Error> {
        let (inner, root) = FatFilesystem::new(disk, FsOptions::new())?;
        Ok(FatFs { inner, root })
    }

    #[cfg(test)]
    pub fn from_filesystem(inner: Pin<Arc<FatFilesystem>>, root: Arc<FatDirectory>) -> Self {
        FatFs { inner, root }
    }

    #[cfg(test)]
    pub fn get_fatfs_root(&self) -> Arc<FatDirectory> {
        self.root.clone()
    }

    #[cfg(test)]
    pub fn filesystem(&self) -> &FatFilesystem {
        return &self.inner;
    }

    /// Get the root directory of this filesystem.
    pub fn get_root(&self) -> Arc<dyn DirectoryEntry> {
        self.root.clone()
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
                let mut result = FilesystemInfo::empty();
                // TODO(simonshields): We should be able to expose more fields here.
                if query.contains(FilesystemInfoQuery::FsType) {
                    result.fs_type = Some(FsType::Fatfs);
                }
                responder.send(&mut Ok(result))?;
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
        fidl_fuchsia_io::{DirectoryProxy, FileProxy, NodeMarker, NodeProxy, OPEN_RIGHT_READABLE},
        fuchsia_async as fasync,
        fuchsia_zircon::Status,
        futures::{future::BoxFuture, prelude::*},
        std::{collections::HashMap, io::Write},
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
            let wrapper: Box<dyn ReadWriteSeek + Send> = Box::new(std::io::Cursor::new(buffer));
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

    #[fasync::run_singlethreaded(test)]
    async fn test_create_disk() {
        let disk = TestFatDisk::empty_disk(2048 << 10); // 2048K disk.

        let structure = TestDiskContents::dir()
            .add_child("test", "This is a test file".into())
            .add_child("empty_folder", TestDiskContents::dir());

        structure.create(&disk.root_dir());

        let fatfs = disk.into_fatfs();
        let scope = ExecutionScope::from_executor(Box::new(fasync::EHandle::local()));
        let (proxy, remote) = fidl::endpoints::create_proxy::<NodeMarker>().unwrap();
        let root = fatfs.get_root();
        root.open(scope, OPEN_RIGHT_READABLE, 0, Path::empty(), remote);

        structure.verify(proxy).await.expect("Verify succeeds");
    }
}
