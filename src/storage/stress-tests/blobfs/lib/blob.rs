// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io::{
        OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE,
    },
    fuchsia_merkle::MerkleTree,
    fuchsia_zircon::Status,
    stress_test_utils::{
        data::FileData,
        io::{Directory, File},
    },
};

pub struct Blob {
    merkle_root_hash: String,
    data: FileData,
    handles: Vec<File>,
}

impl Blob {
    // Attempts to write the blob to disk. This operation is allowed to fail
    // if we run out of storage space. Other failures cause a panic.
    pub async fn create(data: FileData, root_dir: &Directory) -> Result<Self, Status> {
        // Create the root hash for the blob
        let data_bytes = data.generate_bytes();
        let tree = MerkleTree::from_reader(&data_bytes[..]).unwrap();
        let merkle_root_hash = tree.root().to_string();

        // Write the file to disk
        let file = root_dir
            .open_file(
                &merkle_root_hash,
                OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT | OPEN_RIGHT_WRITABLE,
            )
            .await?;
        file.truncate(data.size_bytes).await?;
        file.write(&data_bytes).await?;
        file.close().await?;

        Ok(Self { merkle_root_hash, data, handles: vec![] })
    }

    // Reads the blob in from disk and verifies its contents
    pub async fn verify_from_disk(&self, root_dir: &Directory) -> Result<(), Status> {
        let file = root_dir.open_file(&self.merkle_root_hash, OPEN_RIGHT_READABLE).await?;
        let on_disk_data_bytes = file.read_until_eof().await?;
        file.close().await?;

        let in_memory_data_bytes = self.data.generate_bytes();

        assert!(on_disk_data_bytes == in_memory_data_bytes);

        Ok(())
    }

    pub fn merkle_root_hash(&self) -> &str {
        &self.merkle_root_hash
    }

    pub fn handles(&mut self) -> &mut Vec<File> {
        &mut self.handles
    }

    pub fn data(&self) -> &FileData {
        &self.data
    }

    pub fn num_handles(&self) -> u64 {
        self.handles.len() as u64
    }
}
