// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// For now, we will only interact with a single partition, which must match:
// * partition GUID 08185F0C-892D-428A-A789-DBEEC8F55E6A
//   ( aka GUID_DATA_* from //zircon/system/public/zircon/hw/gpt.h )
// * partition label "account"
// In the fullness of time, we'll probably need to support detecting multiple possible volumes, and
// they may have different labels to better map between partitions and account ids.
pub const FUCHSIA_DATA_GUID: [u8; 16] = [
    // 08185F0C-892D-428A-A789-DBEEC8F55E6A
    0x0c, 0x5f, 0x18, 0x08, 0x2d, 0x89, 0x8a, 0x42, 0xa7, 0x89, 0xdb, 0xee, 0xc8, 0xf5, 0x5e, 0x6a,
];
pub const ACCOUNT_LABEL: &str = "account";
