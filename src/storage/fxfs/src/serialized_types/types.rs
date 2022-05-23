// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    lsm_tree::LayerInfo,
    object_store::{
        transaction::Mutation, AllocatorInfo, AllocatorInfoV1, AllocatorKey, AllocatorValue,
        EncryptedMutations, JournalRecord, ObjectKey, ObjectValue, StoreInfo, StoreInfoV1,
        SuperBlock, SuperBlockRecord,
    },
    serialized_types::{versioned_type, Version, Versioned, VersionedLatest},
};

/// The latest version of on-disk filesystem format.
///
/// If all layer files are compacted the the journal flushed, and super-block
/// both rewritten, all versions should match this value.
pub const LATEST_VERSION: Version = Version { major: 18, minor: 0 };

versioned_type! {
    18.. => AllocatorInfo,
    16.. => AllocatorInfoV1,
}
versioned_type! {
    1.. => AllocatorKey,
}
versioned_type! {
    12.. => AllocatorValue,
}
versioned_type! {
    5.. => EncryptedMutations,
}
versioned_type! {
    15.. => JournalRecord,
}
versioned_type! {
    1.. => LayerInfo,
}
versioned_type! {
    15.. => Mutation,
}
versioned_type! {
    5.. => ObjectKey,
}
versioned_type! {
    5.. => ObjectValue,
}
versioned_type! {
    17.. => StoreInfo,
    8.. => StoreInfoV1,
}
versioned_type! {
    16.. => SuperBlock,
}
versioned_type! {
    5.. => SuperBlockRecord,
}
