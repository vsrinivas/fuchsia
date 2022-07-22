// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fuchsia_hash::Hash;
use serde::Serialize;

#[derive(Debug, Serialize, Eq, PartialEq, Default)]
pub struct PackageSizeInfo {
    pub name: String,
    /// Space used by this package in blobfs if each blob is counted fully.
    pub used_space_in_blobfs: u64,
    /// Size of the package in blobfs if each blob is divided equally among all the packages that reference it.
    pub proportional_size: u64,
    /// Blobs in this package and information about their size.
    pub blobs: Vec<PackageBlobSizeInfo>,
}

#[derive(Debug, Serialize, Eq, PartialEq)]
pub struct PackageBlobSizeInfo {
    pub merkle: Hash,
    pub path_in_package: String,
    /// Space used by this blob in blobfs
    pub used_space_in_blobfs: u64,
    /// Number of occurrences of the blob across all packages.
    pub share_count: u64,
}
