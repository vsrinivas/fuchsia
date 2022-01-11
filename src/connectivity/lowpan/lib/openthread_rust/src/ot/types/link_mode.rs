// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

bitflags::bitflags! {
    /// Link Mode Config.
    /// Functional equivalent of [`otsys::otLinkModeConfig`](crate::otsys::otLinkModeConfig).
    #[repr(C)]
    #[derive(Default)]
    pub struct LinkModeConfig : u8 {
        /// Set if the sender is a Full Thread Device (FTD); clear if a Minimal Thread Device (MTD).
        ///
        /// See page 4-8 of the Thread 1.1.1 specification for more details.
        const IS_FTD = (1<<1);

        /// Set if the sender requires the full Network Data; clear if the sender only needs the stable Network Data.
        ///
        /// See page 4-8 of the Thread 1.1.1 specification for more details.
        const NETWORK_DATA = (1<<0);

        /// Set if the sender has its receiver on when not transmitting, cleared otherwise.
        /// Only an MTD acting as a SED will set this flag.
        ///
        /// See page 4-8 of the Thread 1.1.1 specification for more details.
        const RX_ON_WHEN_IDLE = (1<<3);
    }
}

impl LinkModeConfig {
    /// Returns true if the mode indicates a Full Thread Device (FTD)
    pub fn is_ftd(&self) -> bool {
        self.contains(Self::IS_FTD)
    }

    /// Returns true if the mode indicates a Minimal Thread Device (MTD)
    pub fn is_mtd(&self) -> bool {
        !self.is_ftd()
    }
}

impl From<otLinkModeConfig> for LinkModeConfig {
    fn from(x: otLinkModeConfig) -> Self {
        let mut ret = Self::default();
        if x.mDeviceType() {
            ret |= LinkModeConfig::IS_FTD;
        }
        if x.mNetworkData() {
            ret |= LinkModeConfig::NETWORK_DATA;
        }
        if x.mRxOnWhenIdle() {
            ret |= LinkModeConfig::RX_ON_WHEN_IDLE;
        }
        ret
    }
}

impl From<LinkModeConfig> for otLinkModeConfig {
    fn from(x: LinkModeConfig) -> Self {
        let mut ret = Self::default();
        ret.set_mDeviceType(x.contains(LinkModeConfig::IS_FTD));
        ret.set_mNetworkData(x.contains(LinkModeConfig::NETWORK_DATA));
        ret.set_mRxOnWhenIdle(x.contains(LinkModeConfig::RX_ON_WHEN_IDLE));

        ret
    }
}
