// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    lsm_tree::LayerInfo,
    object_store::{
        transaction::Mutation, AllocatorInfo, AllocatorKey, AllocatorValue, EncryptedMutations,
        JournalRecord, ObjectKey, ObjectValue, StoreInfo, SuperBlock, SuperBlockRecord,
    },
    serialized_types::{versioned_type, Version, Versioned, VersionedLatest},
};

/// The latest version of on-disk filesystem format.
///
/// If all layer files are compacted the the journal flushed, and super-block
/// both rewritten, all versions should match this value.
///
/// Last breaking change:
///  v9:  Track the ObjectStore ID for all allocation extents so we can delete encrypted volumes.
pub const LATEST_VERSION: Version = Version { major: 9, minor: 0 };

versioned_type! {
    2.. => AllocatorInfo,
}
versioned_type! {
    1.. => AllocatorKey,
}
versioned_type! {
    9.. => AllocatorValue,
}
versioned_type! {
    5.. => EncryptedMutations,
}
versioned_type! {
    5.. => JournalRecord,
}
versioned_type! {
    1.. => LayerInfo,
}
versioned_type! {
    5.. => Mutation,
}
versioned_type! {
    5.. => ObjectKey,
}
versioned_type! {
    5.. => ObjectValue,
}
versioned_type! {
    8.. => StoreInfo,
}
versioned_type! {
    7.. => SuperBlock,
    6.. => SuperBlock,
}
versioned_type! {
    5.. => SuperBlockRecord,
}
