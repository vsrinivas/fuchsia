// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    lsm_tree::LayerInfo,
    object_store::{
        transaction::{Mutation, MutationV1, MutationV2},
        AllocatorInfo, AllocatorKey, AllocatorValue, EncryptedMutations, JournalRecord,
        JournalRecordV1, JournalRecordV2, ObjectKey, ObjectValue, StoreInfo, SuperBlock,
        SuperBlockRecord,
    },
    serialized_types::{versioned_type, Version, Versioned, VersionedLatest},
};

/// The latest version of on-disk filesystem format.
///
/// If all layer files are compacted the the journal flushed, and super-block
/// both rewritten, all versions should match this value.
///
/// Last breaking change:
///  v13:  Enable wrapping object keys with AES-GCM-SIV.
pub const LATEST_VERSION: Version = Version { major: 15, minor: 0 };

versioned_type! {
    2.. => AllocatorInfo,
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
    14.. => JournalRecordV2,
    13.. => JournalRecordV1,
}
versioned_type! {
    1.. => LayerInfo,
}
versioned_type! {
    15.. => Mutation,
    14.. => MutationV2,
    13.. => MutationV1,
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
    13.. => SuperBlock,
}
versioned_type! {
    5.. => SuperBlockRecord,
}
