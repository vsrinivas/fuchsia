// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io::*,
    files_async::*,
    fuchsia_zircon::Status,
    io_util::{directory::*, file::*},
    std::path::PathBuf,
};

// A convenience wrapper over a FIDL DirectoryProxy.
// Functions of this struct do not tolerate errors and will panic when they encounter them.
pub struct Directory {
    proxy: DirectoryProxy,
}

impl Directory {
    // Create a Directory object from a path in the namespace
    pub fn from_namespace(path: PathBuf) -> Directory {
        let path_str = path.into_os_string().into_string().unwrap();
        let proxy = io_util::directory::open_in_namespace(
            &path_str,
            OPEN_RIGHT_WRITABLE | OPEN_RIGHT_READABLE,
        )
        .unwrap();
        Directory { proxy }
    }

    // Create a new file in the directory with a given |filename|.
    pub async fn create(&self, filename: &str) -> File {
        let proxy = open_file(
            &self.proxy,
            filename,
            OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT | OPEN_RIGHT_WRITABLE,
        )
        .await
        .unwrap();
        File { proxy }
    }

    // Open a file that already exists in the directory with the given |filename|.
    pub async fn open(&self, filename: &str) -> File {
        let proxy = open_file(&self.proxy, filename, OPEN_RIGHT_READABLE).await.unwrap();
        File { proxy }
    }

    // Delete a file from the directory
    pub async fn remove(&self, filename: &str) {
        let raw_status_code = self.proxy.unlink(filename).await.unwrap();
        Status::ok(raw_status_code).unwrap();
    }

    // Return a list of filenames in the directory
    pub async fn entries(&self) -> Vec<String> {
        readdir(&self.proxy)
            .await
            .unwrap()
            .iter()
            .map(|entry| {
                assert_eq!(entry.kind, DirentKind::File);
                entry.name.clone()
            })
            .collect()
    }
}

// A convenience wrapper over a FIDL FileProxy.
// Most functions of this struct do not tolerate errors and will panic when they encounter them.
#[derive(Debug)]
pub struct File {
    proxy: FileProxy,
}

impl File {
    // Set the length of the file
    pub async fn truncate(&self, length: u64) {
        let raw_status_code = self.proxy.truncate(length).await.unwrap();

        // TODO(58749): Bubble the status code up
        Status::ok(raw_status_code).unwrap();
    }

    // Write the contents of |data| to the file.
    // This function will return an error if the filesystem reports there is no space left.
    // All other errors will cause a panic.
    pub async fn write(&self, data: &Vec<u8>) -> Result<(), Status> {
        let length = data.len() as u64;
        let mut cur: u64 = 0;
        while cur < length {
            let end = std::cmp::min(cur + MAX_BUF, length);
            let to_write = &data[cur as usize..end as usize];
            let (raw_status_code, bytes_written) = self.proxy.write(to_write).await.unwrap();

            Status::ok(raw_status_code)?;

            assert!(bytes_written != 0);
            cur += bytes_written;
        }

        Ok(())
    }

    // Flush all writes to this file to disk
    pub async fn flush(&self) {
        let raw_status_code = self.proxy.sync().await.unwrap();

        // TODO(58749): Bubble the status code up
        Status::ok(raw_status_code).unwrap();
    }

    // Get the size of this file as it exists on disk
    pub async fn size_on_disk(&self) -> u64 {
        let (raw_status_code, attr) = self.proxy.get_attr().await.unwrap();

        // TODO(58749): Bubble the status code up
        Status::ok(raw_status_code).unwrap();
        attr.storage_size
    }

    // Get the uncompressed size of this file (as it would exist in memory)
    pub async fn uncompressed_size(&self) -> u64 {
        let (raw_status_code, attr) = self.proxy.get_attr().await.unwrap();

        // TODO(58749): Bubble the status code up
        Status::ok(raw_status_code).unwrap();
        attr.content_size
    }

    // Read exactly |num_bytes| of the file from the current offset.
    pub async fn read(&self, mut num_bytes: u64) -> Vec<u8> {
        let mut data = vec![];

        // Read in chunks of |MAX_BUF| bytes.
        // This is the maximum buffer size supported over FIDL.
        while num_bytes > 0 {
            let bytes_to_read = std::cmp::min(num_bytes, fidl_fuchsia_io::MAX_BUF);
            let (raw_status_code, mut bytes) = self.proxy.read(bytes_to_read).await.unwrap();

            // TODO(58749): Bubble the status code up
            Status::ok(raw_status_code).unwrap();
            assert_eq!(bytes.len() as u64, bytes_to_read);
            data.append(&mut bytes);
            num_bytes -= bytes_to_read;
        }

        data
    }

    // Read from the current offset until EOF
    pub async fn read_until_eof(&self) -> Vec<u8> {
        read(&self.proxy).await.unwrap()
    }

    // Set the offset of the file relative to the start
    pub async fn seek_from_start(&self, offset: u64) {
        let (raw_status_code, _) = self.proxy.seek(offset as i64, SeekOrigin::Start).await.unwrap();

        // TODO(58749): Bubble the status code up
        Status::ok(raw_status_code).unwrap();
    }

    // Gracefully close the file by informing the filesystem
    pub async fn close(self) {
        io_util::file::close(self.proxy).await.unwrap();
    }
}

impl Clone for Directory {
    fn clone(&self) -> Self {
        let new_proxy = clone_no_describe(&self.proxy, Some(CLONE_FLAG_SAME_RIGHTS)).unwrap();
        Self { proxy: new_proxy }
    }
}
