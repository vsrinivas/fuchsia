// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Logging Region.
/// Functional equivalent of [`otsys::otLogRegion`](crate::otsys::otLogRegion).
#[derive(Debug, Copy, Clone, Eq, Ord, PartialOrd, PartialEq, num_derive::FromPrimitive)]
#[allow(missing_docs)]
pub enum LogRegion {
    Api = otLogRegion_OT_LOG_REGION_API as isize,
    Mle = otLogRegion_OT_LOG_REGION_MLE as isize,
    Arp = otLogRegion_OT_LOG_REGION_ARP as isize,
    NetData = otLogRegion_OT_LOG_REGION_NET_DATA as isize,
    Icmp = otLogRegion_OT_LOG_REGION_ICMP as isize,
    Ip6 = otLogRegion_OT_LOG_REGION_IP6 as isize,
    Tcp = otLogRegion_OT_LOG_REGION_TCP as isize,
    Mac = otLogRegion_OT_LOG_REGION_MAC as isize,
    Mem = otLogRegion_OT_LOG_REGION_MEM as isize,
    Ncp = otLogRegion_OT_LOG_REGION_NCP as isize,
    MeshCop = otLogRegion_OT_LOG_REGION_MESH_COP as isize,
    NetDiag = otLogRegion_OT_LOG_REGION_NET_DIAG as isize,
    Platform = otLogRegion_OT_LOG_REGION_PLATFORM as isize,
    Coap = otLogRegion_OT_LOG_REGION_COAP as isize,
    Cli = otLogRegion_OT_LOG_REGION_CLI as isize,
    Core = otLogRegion_OT_LOG_REGION_CORE as isize,
    Util = otLogRegion_OT_LOG_REGION_UTIL as isize,
    Bbr = otLogRegion_OT_LOG_REGION_BBR as isize,
    Mlr = otLogRegion_OT_LOG_REGION_MLR as isize,
    Dua = otLogRegion_OT_LOG_REGION_DUA as isize,
    Br = otLogRegion_OT_LOG_REGION_BR as isize,
    Srp = otLogRegion_OT_LOG_REGION_SRP as isize,
    Dns = otLogRegion_OT_LOG_REGION_DNS as isize,
}

impl From<otLogRegion> for LogRegion {
    fn from(x: otLogRegion) -> Self {
        use num::FromPrimitive;
        Self::from_u64(x as u64).expect(format!("Unknown otLogRegion value: {}", x).as_str())
    }
}

impl From<LogRegion> for otLogRegion {
    fn from(x: LogRegion) -> Self {
        x as otLogRegion
    }
}

/// Logging Level.
/// Functional equivalent of [`otsys::otLogLevel`](crate::otsys::otLogLevel).
#[derive(Debug, Copy, Clone, Eq, Ord, PartialOrd, PartialEq, num_derive::FromPrimitive)]
#[allow(missing_docs)]
pub enum LogLevel {
    Crit = OT_LOG_LEVEL_CRIT as isize,
    Warn = OT_LOG_LEVEL_WARN as isize,
    Note = OT_LOG_LEVEL_NOTE as isize,
    Info = OT_LOG_LEVEL_INFO as isize,
    Debg = OT_LOG_LEVEL_DEBG as isize,
    None = OT_LOG_LEVEL_NONE as isize,
}

impl From<otLogLevel> for LogLevel {
    fn from(x: otLogLevel) -> Self {
        use num::FromPrimitive;
        Self::from_u64(x as u64).expect(format!("Unknown otLogLevel value: {}", x).as_str())
    }
}

impl From<LogLevel> for otLogLevel {
    fn from(x: LogLevel) -> Self {
        x as otLogLevel
    }
}
