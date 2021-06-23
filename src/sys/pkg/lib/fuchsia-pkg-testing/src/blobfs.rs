// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Fake implementation of blobfs for blobfs::Client.

use {fidl_fuchsia_io::DirectoryProxy, fuchsia_hash::Hash, std::fs::File, tempfile::TempDir};

/// A fake blobfs backed by temporary storage.
/// The name of the blob file is not guaranteed to match the merkle root of the content.
/// Be aware that this implementation does not send USER_0 signal, so `has_blob()` will always
/// return false.
pub struct Fake {
    root: TempDir,
}

impl Fake {
    /// Creates a new fake blobfs and client.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn new() -> (Self, blobfs::Client) {
        let fake = Self { root: TempDir::new().unwrap() };
        let blobfs = blobfs::Client::new(fake.root_proxy());
        (fake, blobfs)
    }

    /// Add a new blob to fake blobfs.
    ///
    /// # Panics
    ///
    /// Panics on error
    pub fn add_blob(&self, hash: Hash, data: impl AsRef<[u8]>) {
        std::fs::write(self.root.path().join(hash.to_string()), data).unwrap();
    }

    fn root_proxy(&self) -> DirectoryProxy {
        DirectoryProxy::new(
            fuchsia_async::Channel::from_channel(
                fdio::transfer_fd(File::open(self.root.path()).unwrap()).unwrap().into(),
            )
            .unwrap(),
        )
    }
}
