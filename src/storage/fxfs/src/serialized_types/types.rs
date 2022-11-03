// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    lsm_tree::LayerInfo,
    object_store::{
        allocator::AllocatorInfoV18, transaction::Mutation, AllocatorInfo, AllocatorKey,
        AllocatorValue, EncryptedMutations, JournalRecord, ObjectKey, ObjectValue, StoreInfo,
        SuperBlock, SuperBlockRecord,
    },
    serialized_types::{versioned_type, Version, Versioned, VersionedLatest},
};

/// The latest version of on-disk filesystem format.
///
/// If all layer files are compacted the the journal flushed, and super-block
/// both rewritten, all versions should match this value.
///
/// If making a breaking change, please see EARLIEST_SUPPORTED_VERSION (below).
///
/// IMPORTANT: When changing this (major or minor), update the list of possible versions at
/// https://cs.opensource.google/fuchsia/fuchsia/+/main:third_party/cobalt_config/fuchsia/local_storage/versions.txt.
pub const LATEST_VERSION: Version = Version { major: 24, minor: 0 };

/// The earliest supported version of the on-disk filesystem format.
///
/// When a breaking change is made:
/// 1) LATEST_VERSION should have it's major component increased (see above).
/// 2) EARLIEST_SUPPORTED_VERSION should be set to the new LATEST_VERSION.
/// 3) The SuperBlock version (below) should also be set to the new LATEST_VERSION.
pub const EARLIEST_SUPPORTED_VERSION: Version = Version { major: 21, minor: 0 };

versioned_type! {
    24.. => AllocatorInfo,
    18.. => AllocatorInfoV18,
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
    20.. => JournalRecord,
}
versioned_type! {
    1.. => LayerInfo,
}
versioned_type! {
    20.. => Mutation,
}
versioned_type! {
    5.. => ObjectKey,
}
versioned_type! {
    5.. => ObjectValue,
}
versioned_type! {
    17.. => StoreInfo,
}
versioned_type! {
    21.. => SuperBlock,
}
versioned_type! {
    5.. => SuperBlockRecord,
}

#[cfg(test)]
fn assert_type_hash<T: type_hash::TypeHash>(hash: u64) -> bool {
    if T::type_hash() != hash {
        eprintln!("{} hash changed to {:#018x}", std::any::type_name::<T>(), T::type_hash());
        false
    } else {
        true
    }
}

#[test]
fn type_hashes() {
    // These hashes should only ever change when adding a new version.
    // The checks below are to ensure that we don't inadvertently change a serialized type.
    // Every versioned_type above should have a corresponding line entry here.
    let mut success = true;
    success &= assert_type_hash::<AllocatorInfo>(0x16bb1be7c14431ec);
    success &= assert_type_hash::<AllocatorKey>(0xe1f0c79ca78a2314);
    success &= assert_type_hash::<AllocatorValue>(0x3c75b908c6b1d289);
    success &= assert_type_hash::<EncryptedMutations>(0x960347c6c0713e58);
    success &= assert_type_hash::<JournalRecord>(0xeac0dd80fce9d3d2);
    success &= assert_type_hash::<LayerInfo>(0x265c7729385ff919);
    success &= assert_type_hash::<Mutation>(0xed73df89d6cc81ed);
    success &= assert_type_hash::<ObjectKey>(0x76c5b15487ebdda8);
    success &= assert_type_hash::<ObjectValue>(0x50773bf339511f2d);
    success &= assert_type_hash::<StoreInfo>(0xa6fecf8e27518741);
    success &= assert_type_hash::<SuperBlock>(0x8fe8278c51bf827e);
    success &= assert_type_hash::<SuperBlockRecord>(0x660ffe56acbf9661);
    assert!(success, "One or more versioned types have different TypeHash signatures.");
}
