// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::extra_hash_descriptor::ExtraHashDescriptor;
use crate::vfs::FilesystemProvider;
use anyhow::Result;
use std::path::{Path, PathBuf};
use vbmeta::{HashDescriptor, Key, Salt, VBMeta};

pub fn sign<FSP: FilesystemProvider>(
    name: impl AsRef<str>,
    image_path: impl AsRef<Path>,
    key: impl AsRef<Path>,
    key_metadata: impl AsRef<Path>,
    additional_descriptors: Vec<PathBuf>,
    salt: Salt,
    fs: &FSP,
) -> Result<(VBMeta, Salt)> {
    // Read the signing key's bytes and metadata.
    let key_pem = fs.read_to_string(key)?;
    let key_metadata = fs.read(key_metadata)?;
    // And then create the signing key from those.
    let key = Key::try_new(&key_pem, key_metadata).unwrap();

    // If any additional files were specified for reading descriptor information
    // from, read them in.
    let mut descriptors = Vec::new();

    for path in additional_descriptors {
        let descriptor_json = fs.read(path)?;
        let descriptor: ExtraHashDescriptor = serde_json::from_slice(descriptor_json.as_slice())?;
        descriptors.push(descriptor.into());
    }

    // Read the image into memory, so that it can be hashed.
    let image = fs.read(image_path)?;

    // Create the descriptor for the image.
    let descriptor = HashDescriptor::new(name.as_ref(), &image, salt.clone());
    descriptors.push(descriptor);

    // And do the signing operation itself.
    VBMeta::sign(descriptors, key).map_err(Into::into).map(|vbmeta| (vbmeta, salt))
}
