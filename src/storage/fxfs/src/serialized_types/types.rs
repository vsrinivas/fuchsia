// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        lsm_tree::LayerInfo,
        object_store::{
            transaction::{Mutation, MutationV1},
            AllocatorInfo, AllocatorKey, AllocatorValue, EncryptedMutations, ExtentKey,
            ExtentValue, JournalRecord, ObjectKey, ObjectValue, ObjectValueV1, StoreInfo,
            StoreInfoV1, SuperBlock, SuperBlockRecord,
        },
        serialized_types::{versioned_type, Version, Versioned, VersionedLatest},
    },
    serde::{Deserialize, Serialize},
};

// If all layer files are compacted the the journal flushed, and super-block both rewritten, all
// versions should match this value.
pub const LATEST_VERSION: Version = Version { major: 4, minor: 0 };

// Note that AllocatorInfoV1 exists only to validate format migrations work.
#[derive(Deserialize, Serialize, Versioned)]
struct AllocatorInfoV1 {
    allocated_bytes: u32,
    layers: Vec<u64>,
}
impl From<AllocatorInfoV1> for AllocatorInfo {
    fn from(f: AllocatorInfoV1) -> Self {
        Self { layers: f.layers, allocated_bytes: f.allocated_bytes as u64 }
    }
}

versioned_type! {
    2.. => AllocatorInfo,
    1.. => AllocatorInfoV1,
}
versioned_type! {
    1.. => AllocatorKey,
}
versioned_type! {
    1.. => AllocatorValue,
}
versioned_type! {
    2.. => EncryptedMutations,
}
versioned_type! {
    1.. => ExtentKey,
}
versioned_type! {
    1.. => ExtentValue,
}
versioned_type! {
    1.. => JournalRecord,
}
versioned_type! {
    1.. => LayerInfo,
}
versioned_type! {
    4.. => Mutation,
    2.. => MutationV1,
}
versioned_type! {
    1.. => ObjectKey,
}
versioned_type! {
    4.. => ObjectValue,
    1.. => ObjectValueV1,
}
versioned_type! {
    4.. => StoreInfo,
    1.. => StoreInfoV1,
}
versioned_type! {
    1.. => SuperBlock,
}
versioned_type! {
    1.. => SuperBlockRecord,
}
