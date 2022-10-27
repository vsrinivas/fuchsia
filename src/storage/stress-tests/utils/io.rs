// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io as fio,
    fuchsia_fs::{directory::*, file::*, node::OpenError},
    fuchsia_zircon::{Event, Status},
    std::path::Path,
    tracing::debug,
};

// A convenience wrapper over a FIDL DirectoryProxy.
// Functions of this struct do not tolerate FIDL errors and will panic when they encounter them.
pub struct Directory {
    proxy: fio::DirectoryProxy,
}

impl Directory {
    // Opens a path in the namespace as a Directory.
    pub fn from_namespace(
        path: impl AsRef<Path>,
        flags: fio::OpenFlags,
    ) -> Result<Directory, Status> {
        let path = path.as_ref().to_str().unwrap();
        match fuchsia_fs::directory::open_in_namespace(path, flags) {
            Ok(proxy) => Ok(Directory { proxy }),
            Err(OpenError::OpenError(s)) => {
                debug!(%path, status = %s, "from_namespace failed");
                Err(s)
            }
            Err(OpenError::SendOpenRequest(e)) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during open: {}", e);
                }
            }
            Err(OpenError::OnOpenEventStreamClosed) => Err(Status::PEER_CLOSED),
            Err(OpenError::Namespace(s)) => Err(s),
            Err(e) => panic!("Unexpected error during open: {}", e),
        }
    }

    // Open a directory in the parent dir with the given |filename|.
    pub async fn open_directory(
        &self,
        filename: &str,
        flags: fio::OpenFlags,
    ) -> Result<Directory, Status> {
        match fuchsia_fs::directory::open_directory(&self.proxy, filename, flags).await {
            Ok(proxy) => Ok(Directory { proxy }),
            Err(OpenError::OpenError(s)) => {
                debug!(%filename, ?flags, status = %s, "open_directory failed");
                Err(s)
            }
            Err(OpenError::SendOpenRequest(e)) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during open: {}", e);
                }
            }
            Err(OpenError::OnOpenEventStreamClosed) => Err(Status::PEER_CLOSED),
            Err(e) => panic!("Unexpected error during open: {}", e),
        }
    }

    // Open a file in the parent dir with the given |filename|.
    pub async fn open_file(&self, filename: &str, flags: fio::OpenFlags) -> Result<File, Status> {
        match fuchsia_fs::directory::open_file(&self.proxy, filename, flags).await {
            Ok(proxy) => Ok(File { proxy }),
            Err(OpenError::OpenError(s)) => {
                debug!(%filename, ?flags, status = %s, "open_file failed");
                Err(s)
            }
            Err(OpenError::SendOpenRequest(e)) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during open: {}", e);
                }
            }
            Err(OpenError::OnOpenEventStreamClosed) => Err(Status::PEER_CLOSED),
            Err(e) => panic!("Unexpected error during open: {}", e),
        }
    }

    // Creates a directory named |filename| within this directory.
    pub async fn create_directory(
        &self,
        filename: &str,
        flags: fio::OpenFlags,
    ) -> Result<Directory, Status> {
        match fuchsia_fs::directory::create_directory(&self.proxy, filename, flags).await {
            Ok(proxy) => Ok(Directory { proxy }),
            Err(OpenError::OpenError(s)) => {
                debug!(%filename, ?flags, status = %s, "create_directory failed");
                Err(s)
            }
            Err(OpenError::SendOpenRequest(e)) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during create: {}", e);
                }
            }
            Err(OpenError::OnOpenEventStreamClosed) => Err(Status::PEER_CLOSED),
            Err(e) => panic!("Unexpected error during create: {}", e),
        }
    }

    // Sync a directory
    pub async fn sync_directory(&self) -> Result<(), Status> {
        match self.proxy.sync().await {
            Ok(_) => Ok(()),
            Err(e) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during sync: {}", e);
                }
            }
        }
    }

    // Delete a file from the directory
    pub async fn remove(&self, filename: &str) -> Result<(), Status> {
        match self.proxy.unlink(filename, fio::UnlinkOptions::EMPTY).await {
            Ok(result) => Status::ok(match result {
                Ok(()) => 0,
                Err(status) => status,
            }),
            Err(e) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during remove: {}", e);
                }
            }
        }
    }

    // Renames |src_name| inside the directory to be |dst_name| within |dst_parent|.
    // Neither |src_name| nor |dst_name| may contain '/' except at the end of either string.
    pub async fn rename(
        &self,
        src_name: &str,
        dst_parent: &Directory,
        dst_name: &str,
    ) -> Result<(), Status> {
        let dst_token = match dst_parent.proxy.get_token().await {
            Ok((_raw_status_code, Some(handle))) => Ok(handle),
            Ok((_raw_status_code, None)) => {
                panic!("No handle");
            }
            Err(e) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during rename: {}", e);
                }
            }
        }?;
        match self.proxy.rename(src_name, Event::from(dst_token), dst_name).await {
            Ok(_) => Ok(()),
            Err(e) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during rename: {}", e);
                }
            }
        }
    }

    // Return a list of filenames in the directory
    pub async fn entries(&self) -> Result<Vec<String>, Status> {
        match fuchsia_fs::directory::readdir(&self.proxy).await {
            Ok(entries) => Ok(entries.iter().map(|entry| entry.name.clone()).collect()),
            Err(fuchsia_fs::directory::Error::Fidl(_, e)) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error reading dirents: {}", e);
                }
            }
            Err(fuchsia_fs::directory::Error::ReadDirents(s)) => Err(s),
            Err(e) => {
                panic!("Unexpected error reading dirents: {}", e);
            }
        }
    }
}

// A convenience wrapper over a FIDL FileProxy.
// Functions of this struct do not tolerate FIDL errors and will panic when they encounter them.
#[derive(Debug)]
pub struct File {
    proxy: fio::FileProxy,
}

impl File {
    // Set the length of the file
    pub async fn truncate(&self, length: u64) -> Result<(), Status> {
        match self.proxy.resize(length).await {
            Ok(result) => result.map_err(Status::from_raw),
            Err(e) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during truncate: {}", e);
                }
            }
        }
    }

    // Write the contents of `data` to the file.
    pub async fn write(&self, data: &[u8]) -> Result<(), Status> {
        match write(&self.proxy, data).await {
            Ok(()) => Ok(()),
            Err(WriteError::Fidl(e)) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during write: {}", e);
                }
            }
            Err(WriteError::WriteError(s)) => Err(s),
            Err(e) => panic!("Unexpected error during write: {:?}", e),
        }
    }

    // Get the size of this file as it exists on disk
    pub async fn size_on_disk(&self) -> Result<u64, Status> {
        match self.proxy.get_attr().await {
            Ok((raw_status_code, attr)) => {
                Status::ok(raw_status_code)?;
                Ok(attr.storage_size)
            }
            Err(e) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during size_on_disk: {}", e)
                }
            }
        }
    }

    // Get the uncompressed size of this file (as it would exist in memory)
    pub async fn uncompressed_size(&self) -> Result<u64, Status> {
        match self.proxy.get_attr().await {
            Ok((raw_status_code, attr)) => {
                Status::ok(raw_status_code)?;
                Ok(attr.content_size)
            }
            Err(e) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during size_on_disk: {}", e)
                }
            }
        }
    }

    // Read `num_bytes` of the file from the current offset.
    // This function may return less bytes than expected.
    pub async fn read_num_bytes(&self, num_bytes: u64) -> Result<Vec<u8>, Status> {
        match read_num_bytes(&self.proxy, num_bytes).await {
            Ok(data) => Ok(data),
            Err(ReadError::Fidl(e)) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during read_exact_num_bytes: {}", e);
                }
            }
            Err(ReadError::ReadError(s)) => Err(s),
            Err(e) => panic!("Unexpected error during read: {:?}", e),
        }
    }

    // Read from the current offset until EOF
    pub async fn read_until_eof(&self) -> Result<Vec<u8>, Status> {
        match read(&self.proxy).await {
            Ok(data) => Ok(data),
            Err(ReadError::Fidl(e)) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during read_until_eof: {}", e);
                }
            }
            Err(ReadError::ReadError(s)) => Err(s),
            Err(e) => panic!("Unexpected error during read_until_eof: {:?}", e),
        }
    }

    // Set the offset of the file
    pub async fn seek(&self, origin: fio::SeekOrigin, offset: u64) -> Result<(), Status> {
        match self.proxy.seek(origin, offset as i64).await {
            Ok(result) => result.map_err(Status::from_raw).map(|_: u64| ()),
            Err(e) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during seek: {}", e);
                }
            }
        }
    }

    // Gracefully close the file by informing the filesystem
    pub async fn close(self) -> Result<(), Status> {
        match self.proxy.close().await {
            Ok(result) => result.map_err(Status::from_raw),
            Err(e) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during close: {}", e);
                }
            }
        }
    }
}

impl Clone for Directory {
    fn clone(&self) -> Self {
        let new_proxy =
            clone_no_describe(&self.proxy, Some(fio::OpenFlags::CLONE_SAME_RIGHTS)).unwrap();
        Self { proxy: new_proxy }
    }
}
