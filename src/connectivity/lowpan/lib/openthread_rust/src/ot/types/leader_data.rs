// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// This structure represents the Thread Leader Data.
///
/// Functional equivalent of [`otsys::otLeaderData`](crate::otsys::otLeaderData).
#[derive(Debug, Default, Clone)]
#[repr(transparent)]
pub struct LeaderData(pub otLeaderData);

impl_ot_castable!(LeaderData, otLeaderData);

impl LeaderData {
    /// Full Network Data Version.
    pub fn data_version(&self) -> u8 {
        self.0.mDataVersion
    }

    /// Leader Router ID.
    pub fn leader_router_id(&self) -> u8 {
        self.0.mLeaderRouterId
    }

    /// Partition ID.
    pub fn partition_id(&self) -> u32 {
        self.0.mPartitionId
    }

    /// Stable Network Data Version.
    pub fn stable_data_version(&self) -> u8 {
        self.0.mStableDataVersion
    }

    /// Leader Weight.
    pub fn weighting(&self) -> u8 {
        self.0.mWeighting
    }
}
