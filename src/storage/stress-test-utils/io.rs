// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io::{DirectoryProxy, FileProxy, SeekOrigin, CLONE_FLAG_SAME_RIGHTS},
    files_async::readdir,
    fuchsia_zircon::Status,
    io_util::{directory::*, file::*, node::OpenError},
    std::path::Path,
};

// A convenience wrapper over a FIDL DirectoryProxy.
// Functions of this struct do not tolerate FIDL errors and will panic when they encounter them.
pub struct Directory {
    proxy: DirectoryProxy,
}

impl Directory {
    // Create a Directory object from a path in the namespace
    pub fn from_namespace(path: impl AsRef<Path>, flags: u32) -> Result<Directory, Status> {
        let path = path.as_ref().to_str().unwrap();
        match io_util::directory::open_in_namespace(path, flags) {
            Ok(proxy) => Ok(Directory { proxy }),
            Err(OpenError::OpenError(s)) => Err(s),
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

    // Open a file in the parent dir with the given |filename|.
    pub async fn open_file(&self, filename: &str, flags: u32) -> Result<File, Status> {
        match open_file(&self.proxy, filename, flags).await {
            Ok(proxy) => Ok(File { proxy }),
            Err(OpenError::OpenError(s)) => Err(s),
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

    // Delete a file from the directory
    pub async fn remove(&self, filename: &str) -> Result<(), Status> {
        match self.proxy.unlink(filename).await {
            Ok(raw_status_code) => Status::ok(raw_status_code),
            Err(e) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error during remove: {}", e);
                }
            }
        }
    }

    // Return a list of filenames in the directory
    pub async fn entries(&self) -> Result<Vec<String>, Status> {
        match readdir(&self.proxy).await {
            Ok(entries) => Ok(entries.iter().map(|entry| entry.name.clone()).collect()),
            Err(files_async::Error::Fidl(_, e)) => {
                if e.is_closed() {
                    Err(Status::PEER_CLOSED)
                } else {
                    panic!("Unexpected FIDL error reading dirents: {}", e);
                }
            }
            Err(files_async::Error::ReadDirents(s)) => Err(s),
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
    proxy: FileProxy,
}

impl File {
    // Set the length of the file
    pub async fn truncate(&self, length: u64) -> Result<(), Status> {
        match self.proxy.truncate(length).await {
            Ok(raw_status_code) => Status::ok(raw_status_code),
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
        let (raw_status_code, attr) = self.proxy.get_attr().await.unwrap();
        Status::ok(raw_status_code)?;
        Ok(attr.storage_size)
    }

    // Get the uncompressed size of this file (as it would exist in memory)
    pub async fn uncompressed_size(&self) -> Result<u64, Status> {
        let (raw_status_code, attr) = self.proxy.get_attr().await.unwrap();
        Status::ok(raw_status_code)?;
        Ok(attr.content_size)
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
    pub async fn seek(&self, origin: SeekOrigin, offset: u64) -> Result<(), Status> {
        match self.proxy.seek(offset as i64, origin).await {
            Ok((raw_status_code, _)) => Status::ok(raw_status_code),
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
            Ok(raw_status_code) => Status::ok(raw_status_code),
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
        let new_proxy = clone_no_describe(&self.proxy, Some(CLONE_FLAG_SAME_RIGHTS)).unwrap();
        Self { proxy: new_proxy }
    }
}
