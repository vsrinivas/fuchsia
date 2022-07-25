// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Duration;

pub const FASTBOOT_CHECK_INTERVAL: Duration = Duration::from_secs(10);
pub(crate) const MDNS_BROADCAST_INTERVAL: Duration = Duration::from_secs(20);
pub(crate) const ZEDBOOT_BROADCAST_INTERVAL: Duration = Duration::from_secs(2);
pub(crate) const GRACE_INTERVAL: Duration = Duration::from_secs(5);

// TODO: Update these to use `saturating_add` when it is stable.
pub(crate) const FASTBOOT_MAX_AGE: Duration =
    Duration::from_secs(FASTBOOT_CHECK_INTERVAL.as_secs() + GRACE_INTERVAL.as_secs());
pub(crate) const MDNS_MAX_AGE: Duration =
    Duration::from_secs(MDNS_BROADCAST_INTERVAL.as_secs() + GRACE_INTERVAL.as_secs());
pub(crate) const ZEDBOOT_MAX_AGE: Duration =
    Duration::from_secs(ZEDBOOT_BROADCAST_INTERVAL.as_secs() + GRACE_INTERVAL.as_secs());

/// RCS connection retry delay.
pub(crate) const RETRY_DELAY: Duration = Duration::from_millis(200);

pub mod fastboot;
pub mod logger;
pub mod manual_targets;
mod overnet;
pub mod target;
pub mod target_collection;
pub mod zedboot;
