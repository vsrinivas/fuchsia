// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl_fuchsia_io::{OPEN_FLAG_CREATE, OPEN_FLAG_CREATE_IF_ABSENT, OPEN_RIGHT_WRITABLE},
    fuchsia_merkle::MerkleTree,
    fuchsia_zircon::Status,
    storage_stress_test_utils::{data::FileFactory, io::Directory},
    stress_test::actor::{Actor, ActorError},
};

// Performs operations on blobs expected to exist on disk
pub struct BlobActor {
    // Factory used to generate blobs of specific size and compressibility
    pub factory: FileFactory,

    // Blobfs root directory
    pub root_dir: Directory,
}

impl BlobActor {
    pub fn new(factory: FileFactory, root_dir: Directory) -> Self {
        Self { factory, root_dir }
    }

    async fn create_blob(&mut self) -> Result<(), Status> {
        // Create the root hash for the blob
        let data_bytes = self.factory.generate_bytes();
        let tree = MerkleTree::from_reader(&data_bytes[..]).unwrap();
        let merkle_root_hash = tree.root().to_string();

        // Write the file to disk
        let file = self
            .root_dir
            .open_file(
                &merkle_root_hash,
                OPEN_FLAG_CREATE | OPEN_FLAG_CREATE_IF_ABSENT | OPEN_RIGHT_WRITABLE,
            )
            .await?;
        file.truncate(data_bytes.len() as u64).await?;
        file.write(&data_bytes).await?;
        file.close().await
    }
}

#[async_trait]
impl Actor for BlobActor {
    async fn perform(&mut self) -> Result<(), ActorError> {
        match self.create_blob().await {
            Ok(()) => Ok(()),
            Err(Status::NO_SPACE) => Ok(()),
            Err(Status::CONNECTION_ABORTED)
            | Err(Status::PEER_CLOSED)
            | Err(Status::IO)
            | Err(Status::IO_REFUSED) => Err(ActorError::ResetEnvironment),
            Err(s) => panic!("Error occurred during blob create: {}", s),
        }
    }
}
