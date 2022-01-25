// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    lsm_tree::LayerInfo,
    object_store::{
        AllocatorInfo, AllocatorKey, AllocatorValue, ExtentKey, ExtentValue, JournalRecord,
        ObjectKey, ObjectValue, StoreInfo, SuperBlock, SuperBlockRecord,
    },
    serialized_types::{versioned_type, Version, Versioned, VersionedLatest},
};

// If all layer files are compacted the the journal flushed, and super-block both rewritten, all
// versions should match this value.
//const LATEST_VERSION: u32 = 2;

versioned_type! {
    1 => AllocatorInfo,
    2 => AllocatorInfo,
}
versioned_type! {
    1 => AllocatorKey,
    2 => AllocatorKey,
}
versioned_type! {
    1 => AllocatorValue,
    2 => AllocatorValue,
}
versioned_type! {
    1 => ExtentKey,
    2 => ExtentKey,
}
versioned_type! {
    1 => ExtentValue,
    2 => ExtentValue,
}
versioned_type! {
    1 => JournalRecord,
    2 => JournalRecord,
}
versioned_type! {
    1 => LayerInfo,
    2 => LayerInfo,
}
versioned_type! {
    1 => ObjectKey,
    2 => ObjectKey,
}
versioned_type! {
    1 => ObjectValue,
    2 => ObjectValue,
}
versioned_type! {
    1 => StoreInfo,
    2 => StoreInfo,
}
versioned_type! {
    1 => SuperBlock,
    2 => SuperBlock,
}
versioned_type! {
    1 => SuperBlockRecord,
    2 => SuperBlockRecord,
}
