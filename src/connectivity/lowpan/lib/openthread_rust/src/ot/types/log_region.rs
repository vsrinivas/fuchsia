// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Logging Region.
/// Functional equivalent of [`otsys::otLogRegion`](crate::otsys::otLogRegion).
#[derive(Debug, Copy, Clone, Eq, Ord, PartialOrd, PartialEq, num_derive::FromPrimitive)]
#[allow(missing_docs)]
pub enum LogRegion {
    Api = OT_LOG_REGION_API as isize,
    Mle = OT_LOG_REGION_MLE as isize,
    Arp = OT_LOG_REGION_ARP as isize,
    NetData = OT_LOG_REGION_NET_DATA as isize,
    Icmp = OT_LOG_REGION_ICMP as isize,
    Ip6 = OT_LOG_REGION_IP6 as isize,
    Tcp = OT_LOG_REGION_TCP as isize,
    Mac = OT_LOG_REGION_MAC as isize,
    Mem = OT_LOG_REGION_MEM as isize,
    Ncp = OT_LOG_REGION_NCP as isize,
    MeshCop = OT_LOG_REGION_MESH_COP as isize,
    NetDiag = OT_LOG_REGION_NET_DIAG as isize,
    Platform = OT_LOG_REGION_PLATFORM as isize,
    Coap = OT_LOG_REGION_COAP as isize,
    Cli = OT_LOG_REGION_CLI as isize,
    Core = OT_LOG_REGION_CORE as isize,
    Util = OT_LOG_REGION_UTIL as isize,
    Bbr = OT_LOG_REGION_BBR as isize,
    Mlr = OT_LOG_REGION_MLR as isize,
    Dua = OT_LOG_REGION_DUA as isize,
    Br = OT_LOG_REGION_BR as isize,
    Srp = OT_LOG_REGION_SRP as isize,
    Dns = OT_LOG_REGION_DNS as isize,
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
