// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Spinel Host-NCP Protocol Enumerations and Constants
//!
//! This module contains various enumerations and constants
//! which are defined in [draft-rquattle-spinel-unified-00]
//! and [OpenThread's `spinel.h`][openthread-spinel].
//!
//! [draft-rquattle-spinel-unified-00]: https://tools.ietf.org/html/draft-rquattle-spinel-unified-00
//! [openthread-spinel]: https://github.com/openthread/openthread/blob/master/src/lib/spinel/spinel.h

use spinel_pack::*;

use std::io;

#[allow(unused)]
pub const PROTOCOL_MAJOR_VERSION: u32 = 4;

#[allow(unused)]
pub const PROTOCOL_MINOR_VERSION: u32 = 1;

/// Macro for implementing Spinel integer unpacking traits
/// for types that implement `From<u32>`. Specifically, this
/// implements the following traits for a given type:
///
/// * `TryUnpackAs<SpinelUint>`
/// * `TryOwnedUnpack`
/// * `TryUnpack`
macro_rules! impl_spinel_unpack_uint (
    ($t:ty) => {
        impl<'a> TryUnpackAs<'a, SpinelUint> for $t {
            fn try_unpack_as(iter: &mut std::slice::Iter<'a, u8>) -> anyhow::Result<Self> {
                let id: u32 = TryUnpackAs::<SpinelUint>::try_unpack_as(iter)?;
                Ok(id.into())
            }
        }
        impl_try_unpack_for_owned!{
            impl TryOwnedUnpack for $t {
                type Unpacked = $t;
                fn try_owned_unpack(iter: &mut std::slice::Iter<'_, u8>) -> anyhow::Result<Self::Unpacked> {
                    TryUnpackAs::<SpinelUint>::try_unpack_as(iter)
                }
            }
        }
    };
);

/// Macro for implementing Spinel integer packing traits
/// for types that implement `Into<u32>`. Specifically, this
/// implements the following traits for a given type:
///
/// * `TryPackAs<SpinelUint>`
/// * `TryPack`
macro_rules! impl_spinel_pack_uint (
    ($t:ty) => {
        impl TryPackAs<SpinelUint> for $t {
            fn pack_as_len(&self) -> std::io::Result<usize> {
                TryPackAs::<SpinelUint>::pack_as_len(&u32::from(*self))
            }

            fn try_pack_as<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> std::io::Result<usize> {
                TryPackAs::<SpinelUint>::try_pack_as(&u32::from(*self), buffer)
            }
        }

        impl TryPack for $t {
            fn pack_len(&self) -> io::Result<usize> {
                TryPackAs::<SpinelUint>::pack_as_len(&u32::from(*self))
            }

            fn try_pack<T: std::io::Write + ?Sized>(&self, buffer: &mut T) -> io::Result<usize> {
                TryPackAs::<SpinelUint>::try_pack_as(&u32::from(*self), buffer)
            }
        }
    };
);

/// For enumerations which are subsections of
/// a larger enumeration. Ensures that they also
/// implement all of the same packing traits as
/// the higher-level enumerations.
///
/// These types don't implement the unpacking
/// traits.
macro_rules! impl_sub_enum (
    ($bt:tt :: $v:tt, $t:ty) => {
        impl From<$t> for $bt {
            fn from(prop: $t) -> Self {
                $bt::$v(prop)
            }
        }
        impl From<$t> for u32 {
            fn from(x: $t) -> Self {
                $bt::$v(x).into()
            }
        }
        impl_spinel_pack_uint!($t);
    };
);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum Cmd {
    Noop,
    Reset,
    PropValueGet,
    PropValueSet,
    PropValueInsert,
    PropValueRemove,
    PropValueIs,
    PropValueInserted,
    PropValueRemoved,
    NetSave,
    NetClear,
    NetRecall,
    Unknown(u32),
}
impl_spinel_unpack_uint!(Cmd);
impl_spinel_pack_uint!(Cmd);

impl From<Cmd> for u32 {
    fn from(cmd: Cmd) -> Self {
        use Cmd::*;
        match cmd {
            Noop => 0,
            Reset => 1,
            PropValueGet => 2,
            PropValueSet => 3,
            PropValueInsert => 4,
            PropValueRemove => 5,
            PropValueIs => 6,
            PropValueInserted => 7,
            PropValueRemoved => 8,
            NetSave => 9,
            NetClear => 10,
            NetRecall => 11,
            Unknown(x) => x,
        }
    }
}

impl From<u32> for Cmd {
    fn from(id: u32) -> Self {
        use Cmd::*;
        match id {
            0 => Noop,
            1 => Reset,
            2 => PropValueGet,
            3 => PropValueSet,
            4 => PropValueInsert,
            5 => PropValueRemove,
            6 => PropValueIs,
            7 => PropValueInserted,
            8 => PropValueRemoved,
            9 => NetSave,
            10 => NetClear,
            11 => NetRecall,
            x => Unknown(x),
        }
    }
}

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum PropStream {
    Debug,
    Raw,
    Net,
    NetInsecure,
    Mfg,
    Unknown(u32),
}
impl_sub_enum!(Prop::Stream, PropStream);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum PropGpio {
    Config,     // 0x1000
    State,      // 0x1001
    StateSet,   // 0x1002
    StateClear, // 0x1003
}
impl_sub_enum!(Prop::Gpio, PropGpio);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum PropTrng {
    Num32,  // 0x1005
    Num128, // 0x1006
    Raw32,  // 0x1007
}
impl_sub_enum!(Prop::Trng, PropTrng);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum PropPhy {
    Enabled,
    Chan,
    ChanSupported,
    Freq,
    CcaThreshold,
    TxPower,
    Rssi,
    RxSensitivity,
    PcapEnabled,
    ChanPreferred,
    Unknown(u32),
}
impl_sub_enum!(Prop::Phy, PropPhy);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum PropMac {
    ScanState,
    ScanMask,
    ScanPeriod,
    ScanBeacon,
    LongAddr,
    ShortAddr,
    Panid,
    RawStreamEnabled,
    PromiscuousMode,
    EnergyScanResult,
    DataPollPeriod,

    Unknown(u32),
}
impl_sub_enum!(Prop::Mac, PropMac);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum PropNet {
    Saved,
    InterfaceUp,
    StackUp,
    Role,
    NetworkName,
    Xpanid,
    MasterKey,
    KeySequenceCounter,
    PartitionId,
    RequireJoinExisting,
    KeySwitchGuardtime,
    Pskc,

    Unknown(u32),
}
impl_sub_enum!(Prop::Net, PropNet);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum PropThread {
    LeaderAddr,
    Parent,
    ChildTable,
    LeaderRid,
    LeaderWeight,
    LocalLeaderWeight,
    NetworkData,
    NetworkDataVersion,
    StableNetworkData,
    StableNetworkDataVersion,
    OnMeshNets,
    OffMeshRoutes,
    AssistingPorts,
    AllowLocalNetDataChange,
    Mode,

    ChildTimeout,
    Rloc16,
    RouterUpgradeThreshold,
    ContextReuseDelay,
    NetworkIdTimeout,
    ActiveRouterIds,
    Rloc16DebugPassthru,
    RouterRoleEnabled,
    RouterDowngradeThreshold,
    RouterSelectionJitter,
    PreferredRouterId,
    NeighborTable,
    ChildCountMax,
    LeaderNetworkData,
    StableLeaderNetworkData,
    Joiners,
    CommissionerEnabled,
    DiscoveryScanJoinerFlag,
    DiscoveryScanEnableFiltering,
    DiscoveryScanPanid,
    SteeringData,
    RouterTable,
    ActiveDataset,
    PendingDataset,
    MgmtSetActiveDataset,
    MgmtSetPendingDataset,

    Unknown(u32),
}
impl_sub_enum!(Prop::Thread, PropThread);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum PropIpv6 {
    LlAddr,
    MlAddr,
    MlPrefix,
    AddressTable,
    RouteTable,
    IcmpPingOffload,
    MulticastAddressTable,
    IcmpPingOffloadMode,

    Unknown(u32),
}
impl_sub_enum!(Prop::Ipv6, PropIpv6);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum Prop {
    LastStatus,
    ProtocolVersion,
    NcpVersion,
    InterfaceType,
    InterfaceVendorId,
    Caps,
    InterfaceCount,
    PowerState,
    HwAddr,
    Lock,
    HostPowerState,
    McuPowerState,
    Gpio(PropGpio),
    Trng(PropTrng),
    Stream(PropStream),
    Phy(PropPhy),
    Mac(PropMac),
    Net(PropNet),
    Thread(PropThread),
    Ipv6(PropIpv6),
    Unknown(u32),
}
impl_spinel_pack_uint!(Prop);
impl_spinel_unpack_uint!(Prop);

impl From<Prop> for u32 {
    fn from(prop: Prop) -> Self {
        use Prop::*;
        match prop {
            LastStatus => 0,
            ProtocolVersion => 1,
            NcpVersion => 2,
            InterfaceType => 3,
            InterfaceVendorId => 4,
            Caps => 5,
            InterfaceCount => 6,
            PowerState => 7,
            HwAddr => 8,
            Lock => 9,
            HostPowerState => 12,
            McuPowerState => 13,

            Gpio(PropGpio::Config) => 0x1000,
            Gpio(PropGpio::State) => 0x1002,
            Gpio(PropGpio::StateSet) => 0x1003,
            Gpio(PropGpio::StateClear) => 0x1004,

            Trng(PropTrng::Num32) => 0x1005,
            Trng(PropTrng::Num128) => 0x1006,
            Trng(PropTrng::Raw32) => 0x1007,

            Stream(PropStream::Debug) => 112,
            Stream(PropStream::Raw) => 113,
            Stream(PropStream::Net) => 114,
            Stream(PropStream::NetInsecure) => 115,
            Stream(PropStream::Mfg) => 0x3BC0,
            Stream(PropStream::Unknown(x)) => x,

            Phy(PropPhy::Enabled) => 0x20,
            Phy(PropPhy::Chan) => 0x21,
            Phy(PropPhy::ChanSupported) => 0x22,
            Phy(PropPhy::Freq) => 0x23,
            Phy(PropPhy::CcaThreshold) => 0x24,
            Phy(PropPhy::TxPower) => 0x25,
            Phy(PropPhy::Rssi) => 0x26,
            Phy(PropPhy::RxSensitivity) => 0x27,
            Phy(PropPhy::PcapEnabled) => 0x28,
            Phy(PropPhy::ChanPreferred) => 0x29,
            Phy(PropPhy::Unknown(x)) => x,

            Mac(PropMac::ScanState) => 0x30,
            Mac(PropMac::ScanMask) => 0x31,
            Mac(PropMac::ScanPeriod) => 0x32,
            Mac(PropMac::ScanBeacon) => 0x33,
            Mac(PropMac::LongAddr) => 0x34,
            Mac(PropMac::ShortAddr) => 0x35,
            Mac(PropMac::Panid) => 0x36,
            Mac(PropMac::RawStreamEnabled) => 0x37,
            Mac(PropMac::PromiscuousMode) => 0x38,
            Mac(PropMac::EnergyScanResult) => 0x39,
            Mac(PropMac::DataPollPeriod) => 0x3a,
            Mac(PropMac::Unknown(x)) => x,

            Net(PropNet::Saved) => 0x40,
            Net(PropNet::InterfaceUp) => 0x41,
            Net(PropNet::StackUp) => 0x42,
            Net(PropNet::Role) => 0x43,
            Net(PropNet::NetworkName) => 0x44,
            Net(PropNet::Xpanid) => 0x45,
            Net(PropNet::MasterKey) => 0x46,
            Net(PropNet::KeySequenceCounter) => 0x47,
            Net(PropNet::PartitionId) => 0x48,
            Net(PropNet::RequireJoinExisting) => 0x49,
            Net(PropNet::KeySwitchGuardtime) => 0x4A,
            Net(PropNet::Pskc) => 0x4B,
            Net(PropNet::Unknown(x)) => x,

            Thread(PropThread::LeaderAddr) => 0x50,
            Thread(PropThread::Parent) => 0x51,
            Thread(PropThread::ChildTable) => 0x52,
            Thread(PropThread::LeaderRid) => 0x53,
            Thread(PropThread::LeaderWeight) => 0x54,
            Thread(PropThread::LocalLeaderWeight) => 0x55,
            Thread(PropThread::NetworkData) => 0x56,
            Thread(PropThread::NetworkDataVersion) => 0x57,
            Thread(PropThread::StableNetworkData) => 0x58,
            Thread(PropThread::StableNetworkDataVersion) => 0x59,
            Thread(PropThread::OnMeshNets) => 0x5a,
            Thread(PropThread::OffMeshRoutes) => 0x5b,
            Thread(PropThread::AssistingPorts) => 0x5c,
            Thread(PropThread::AllowLocalNetDataChange) => 0x5d,
            Thread(PropThread::Mode) => 0x5e,
            Thread(PropThread::ChildTimeout) => 0x1500,
            Thread(PropThread::Rloc16) => 0x1501,
            Thread(PropThread::RouterUpgradeThreshold) => 0x1502,
            Thread(PropThread::ContextReuseDelay) => 0x1503,
            Thread(PropThread::NetworkIdTimeout) => 0x1504,
            Thread(PropThread::ActiveRouterIds) => 0x1505,
            Thread(PropThread::Rloc16DebugPassthru) => 0x1506,
            Thread(PropThread::RouterRoleEnabled) => 0x1507,
            Thread(PropThread::RouterDowngradeThreshold) => 0x1508,
            Thread(PropThread::RouterSelectionJitter) => 0x1509,
            Thread(PropThread::PreferredRouterId) => 0x150a,
            Thread(PropThread::NeighborTable) => 0x150b,
            Thread(PropThread::ChildCountMax) => 0x150c,
            Thread(PropThread::LeaderNetworkData) => 0x150d,
            Thread(PropThread::StableLeaderNetworkData) => 0x150e,
            Thread(PropThread::Joiners) => 0x150f,
            Thread(PropThread::CommissionerEnabled) => 0x1510,
            Thread(PropThread::DiscoveryScanJoinerFlag) => 0x1513,
            Thread(PropThread::DiscoveryScanEnableFiltering) => 0x1514,
            Thread(PropThread::DiscoveryScanPanid) => 0x1515,
            Thread(PropThread::SteeringData) => 0x1516,
            Thread(PropThread::RouterTable) => 0x1517,
            Thread(PropThread::ActiveDataset) => 0x1518,
            Thread(PropThread::PendingDataset) => 0x1519,
            Thread(PropThread::MgmtSetActiveDataset) => 0x151a,
            Thread(PropThread::MgmtSetPendingDataset) => 0x151b,
            Thread(PropThread::Unknown(x)) => x,

            Ipv6(PropIpv6::LlAddr) => 0x60,
            Ipv6(PropIpv6::MlAddr) => 0x61,
            Ipv6(PropIpv6::MlPrefix) => 0x62,
            Ipv6(PropIpv6::AddressTable) => 0x63,
            Ipv6(PropIpv6::RouteTable) => 0x64,
            Ipv6(PropIpv6::IcmpPingOffload) => 0x65,
            Ipv6(PropIpv6::MulticastAddressTable) => 0x66,
            Ipv6(PropIpv6::IcmpPingOffloadMode) => 0x67,
            Ipv6(PropIpv6::Unknown(x)) => x,

            Unknown(x) => x,
        }
    }
}

impl From<u32> for Prop {
    fn from(id: u32) -> Self {
        use Prop::*;
        match id {
            0 => LastStatus,
            1 => ProtocolVersion,
            2 => NcpVersion,
            3 => InterfaceType,
            4 => InterfaceVendorId,
            5 => Caps,
            6 => InterfaceCount,
            7 => PowerState,
            8 => HwAddr,
            9 => Lock,
            12 => HostPowerState,
            13 => McuPowerState,

            0x1000 => Gpio(PropGpio::Config),
            0x1002 => Gpio(PropGpio::State),
            0x1003 => Gpio(PropGpio::StateSet),
            0x1004 => Gpio(PropGpio::StateClear),

            0x1005 => Trng(PropTrng::Num32),
            0x1006 => Trng(PropTrng::Num128),
            0x1007 => Trng(PropTrng::Raw32),

            0x20 => Phy(PropPhy::Enabled),
            0x21 => Phy(PropPhy::Chan),
            0x22 => Phy(PropPhy::ChanSupported),
            0x23 => Phy(PropPhy::Freq),
            0x24 => Phy(PropPhy::CcaThreshold),
            0x25 => Phy(PropPhy::TxPower),
            0x26 => Phy(PropPhy::Rssi),
            0x27 => Phy(PropPhy::RxSensitivity),
            0x28 => Phy(PropPhy::PcapEnabled),
            0x29 => Phy(PropPhy::ChanPreferred),
            x if (x >= 0x20 && x < 0x30) || (x >= 0x1200 && x < 0x1300) => Phy(PropPhy::Unknown(x)),

            0x30 => Mac(PropMac::ScanState),
            0x31 => Mac(PropMac::ScanMask),
            0x32 => Mac(PropMac::ScanPeriod),
            0x33 => Mac(PropMac::ScanBeacon),
            0x34 => Mac(PropMac::LongAddr),
            0x35 => Mac(PropMac::ShortAddr),
            0x36 => Mac(PropMac::Panid),
            0x37 => Mac(PropMac::RawStreamEnabled),
            0x38 => Mac(PropMac::PromiscuousMode),
            0x39 => Mac(PropMac::EnergyScanResult),
            0x3a => Mac(PropMac::DataPollPeriod),
            x if (x >= 0x30 && x < 0x40) || (x >= 0x1300 && x < 0x1400) => Mac(PropMac::Unknown(x)),

            0x40 => Net(PropNet::Saved),
            0x41 => Net(PropNet::InterfaceUp),
            0x42 => Net(PropNet::StackUp),
            0x43 => Net(PropNet::Role),
            0x44 => Net(PropNet::NetworkName),
            0x45 => Net(PropNet::Xpanid),
            0x46 => Net(PropNet::MasterKey),
            0x47 => Net(PropNet::KeySequenceCounter),
            0x48 => Net(PropNet::PartitionId),
            0x49 => Net(PropNet::RequireJoinExisting),
            0x4A => Net(PropNet::KeySwitchGuardtime),
            0x4B => Net(PropNet::Pskc),
            x if (x >= 0x40 && x < 0x50) || (x >= 0x1400 && x < 0x1500) => Net(PropNet::Unknown(x)),

            0x50 => Thread(PropThread::LeaderAddr),
            0x51 => Thread(PropThread::Parent),
            0x52 => Thread(PropThread::ChildTable),
            0x53 => Thread(PropThread::LeaderRid),
            0x54 => Thread(PropThread::LeaderWeight),
            0x55 => Thread(PropThread::LocalLeaderWeight),
            0x56 => Thread(PropThread::NetworkData),
            0x57 => Thread(PropThread::NetworkDataVersion),
            0x58 => Thread(PropThread::StableNetworkData),
            0x59 => Thread(PropThread::StableNetworkDataVersion),
            0x5a => Thread(PropThread::OnMeshNets),
            0x5b => Thread(PropThread::OffMeshRoutes),
            0x5c => Thread(PropThread::AssistingPorts),
            0x5d => Thread(PropThread::AllowLocalNetDataChange),
            0x5e => Thread(PropThread::Mode),
            0x1500 => Thread(PropThread::ChildTimeout),
            0x1501 => Thread(PropThread::Rloc16),
            0x1502 => Thread(PropThread::RouterUpgradeThreshold),
            0x1503 => Thread(PropThread::ContextReuseDelay),
            0x1504 => Thread(PropThread::NetworkIdTimeout),
            0x1505 => Thread(PropThread::ActiveRouterIds),
            0x1506 => Thread(PropThread::Rloc16DebugPassthru),
            0x1507 => Thread(PropThread::RouterRoleEnabled),
            0x1508 => Thread(PropThread::RouterDowngradeThreshold),
            0x1509 => Thread(PropThread::RouterSelectionJitter),
            0x150a => Thread(PropThread::PreferredRouterId),
            0x150b => Thread(PropThread::NeighborTable),
            0x150c => Thread(PropThread::ChildCountMax),
            0x150d => Thread(PropThread::LeaderNetworkData),
            0x150e => Thread(PropThread::StableLeaderNetworkData),
            0x150f => Thread(PropThread::Joiners),
            0x1510 => Thread(PropThread::CommissionerEnabled),
            0x1513 => Thread(PropThread::DiscoveryScanJoinerFlag),
            0x1514 => Thread(PropThread::DiscoveryScanEnableFiltering),
            0x1515 => Thread(PropThread::DiscoveryScanPanid),
            0x1516 => Thread(PropThread::SteeringData),
            0x1517 => Thread(PropThread::RouterTable),
            0x1518 => Thread(PropThread::ActiveDataset),
            0x1519 => Thread(PropThread::PendingDataset),
            0x151a => Thread(PropThread::MgmtSetActiveDataset),
            0x151b => Thread(PropThread::MgmtSetPendingDataset),
            x if (x >= 0x50 && x < 0x60) || (x >= 0x1500 && x < 0x1600) => {
                Thread(PropThread::Unknown(x))
            }

            0x60 => Ipv6(PropIpv6::LlAddr),
            0x61 => Ipv6(PropIpv6::MlAddr),
            0x62 => Ipv6(PropIpv6::MlPrefix),
            0x63 => Ipv6(PropIpv6::AddressTable),
            0x64 => Ipv6(PropIpv6::RouteTable),
            0x65 => Ipv6(PropIpv6::IcmpPingOffload),
            0x66 => Ipv6(PropIpv6::MulticastAddressTable),
            0x67 => Ipv6(PropIpv6::IcmpPingOffloadMode),
            x if (x >= 0x60 && x < 0x70) || (x >= 0x1600 && x < 0x1700) => {
                Ipv6(PropIpv6::Unknown(x))
            }

            112 => Stream(PropStream::Debug),
            113 => Stream(PropStream::Raw),
            114 => Stream(PropStream::Net),
            115 => Stream(PropStream::NetInsecure),
            0x3Bc0 => Stream(PropStream::Mfg),
            x if (x >= 0x70 && x < 0x80) || (x >= 0x1700 && x < 0x1800) => {
                Stream(PropStream::Unknown(x))
            }

            x => Unknown(x),
        }
    }
}

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum StatusJoin {
    Failure,      // = 104,
    Security,     // = 105,
    NoPeers,      // = 106,
    Incompatible, // = 107,
    RspTimeout,   // = 108,
    Success,      // = 109,
}
impl_sub_enum!(Status::Join, StatusJoin);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum StatusReset {
    PowerOn,           // = 112,
    External,          // = 113,
    Software,          // = 114,
    Fault,             // = 115,
    Crash,             // = 116,
    Assert,            // = 117,
    Other,             // = 118,
    ExplicitlyUnknown, // = 119,
    Watchdog,          // = 120,
    Unknown(u32),
}
impl_sub_enum!(Status::Reset, StatusReset);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd, thiserror::Error)]
pub enum Status {
    Ok,                    // = 0,
    Failure,               // = 1,
    Unimplemented,         // = 2,
    InvalidArgument,       // = 3,
    InvalidState,          // = 4,
    InvalidCommand,        // = 5,
    InvalidInterface,      // = 6,
    InternalError,         // = 7,
    SecurityError,         // = 8,
    ParseError,            // = 9,
    InProgress,            // = 10,
    Nomem,                 // = 11,
    Busy,                  // = 12,
    PropNotFound,          // = 13,
    Dropped,               // = 14,
    Empty,                 // = 15,
    CmdTooBig,             // = 16,
    NoAck,                 // = 17,
    CcaFailure,            // = 18,
    Already,               // = 19,
    ItemNotFound,          // = 20,
    InvalidCommandForProp, // = 21,

    Join(StatusJoin),

    Reset(StatusReset),

    Vendor(u32),

    StackNative(u32),

    Unknown(u32),
}
impl_spinel_pack_uint!(Status);
impl_spinel_unpack_uint!(Status);

impl std::fmt::Display for Status {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(self, f)
    }
}

impl From<Status> for u32 {
    fn from(status: Status) -> Self {
        use Status::*;
        match status {
            Ok => 0,
            Failure => 1,
            Unimplemented => 2,
            InvalidArgument => 3,
            InvalidState => 4,
            InvalidCommand => 5,
            InvalidInterface => 6,
            InternalError => 7,
            SecurityError => 8,
            ParseError => 9,
            InProgress => 10,
            Nomem => 11,
            Busy => 12,
            PropNotFound => 13,
            Dropped => 14,
            Empty => 15,
            CmdTooBig => 16,
            NoAck => 17,
            CcaFailure => 18,
            Already => 19,
            ItemNotFound => 20,
            InvalidCommandForProp => 21,

            Join(StatusJoin::Failure) => 104,
            Join(StatusJoin::Security) => 105,
            Join(StatusJoin::NoPeers) => 106,
            Join(StatusJoin::Incompatible) => 107,
            Join(StatusJoin::RspTimeout) => 108,
            Join(StatusJoin::Success) => 109,

            Reset(StatusReset::PowerOn) => 112,
            Reset(StatusReset::External) => 113,
            Reset(StatusReset::Software) => 114,
            Reset(StatusReset::Fault) => 115,
            Reset(StatusReset::Crash) => 116,
            Reset(StatusReset::Assert) => 117,
            Reset(StatusReset::Other) => 118,
            Reset(StatusReset::ExplicitlyUnknown) => 119,
            Reset(StatusReset::Watchdog) => 120,
            Reset(StatusReset::Unknown(x)) => x,

            Vendor(x) => x,
            StackNative(x) => x,
            Unknown(x) => x,
        }
    }
}

impl From<u32> for Status {
    fn from(id: u32) -> Self {
        use Status::*;
        match id {
            0 => Ok,
            1 => Failure,
            2 => Unimplemented,
            3 => InvalidArgument,
            4 => InvalidState,
            5 => InvalidCommand,
            6 => InvalidInterface,
            7 => InternalError,
            8 => SecurityError,
            9 => ParseError,
            10 => InProgress,
            11 => Nomem,
            12 => Busy,
            13 => PropNotFound,
            14 => Dropped,
            15 => Empty,
            16 => CmdTooBig,
            17 => NoAck,
            18 => CcaFailure,
            19 => Already,
            20 => ItemNotFound,
            21 => InvalidCommandForProp,

            104 => Join(StatusJoin::Failure),
            105 => Join(StatusJoin::Security),
            106 => Join(StatusJoin::NoPeers),
            107 => Join(StatusJoin::Incompatible),
            108 => Join(StatusJoin::RspTimeout),
            109 => Join(StatusJoin::Success),

            112 => Reset(StatusReset::PowerOn),
            113 => Reset(StatusReset::External),
            114 => Reset(StatusReset::Software),
            115 => Reset(StatusReset::Fault),
            116 => Reset(StatusReset::Crash),
            117 => Reset(StatusReset::Assert),
            118 => Reset(StatusReset::Other),
            119 => Reset(StatusReset::ExplicitlyUnknown),
            120 => Reset(StatusReset::Watchdog),

            x if x >= 0x70 && x < 0x80 => Reset(StatusReset::Unknown(x)),

            x if x >= 15360 && x < 16384 => Vendor(x),
            x if x >= 16384 && x < 81920 => StackNative(x),
            x => Unknown(x),
        }
    }
}

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum NetRole {
    Detached,
    Child,
    Router,
    Leader,
    Unknown(u32),
}
impl_spinel_pack_uint!(NetRole);
impl_spinel_unpack_uint!(NetRole);

impl std::fmt::Display for NetRole {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(self, f)
    }
}

impl From<NetRole> for u32 {
    fn from(role: NetRole) -> Self {
        use NetRole::*;
        match role {
            Detached => 0,
            Child => 1,
            Router => 2,
            Leader => 3,

            Unknown(x) => x,
        }
    }
}

impl From<u32> for NetRole {
    fn from(id: u32) -> Self {
        use NetRole::*;
        match id {
            0 => Detached,
            1 => Child,
            2 => Router,
            3 => Leader,
            x => Unknown(x),
        }
    }
}

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum InterfaceType {
    Bootloader,
    ZigbeeIp,
    Thread,
    Unknown(u32),
}
impl_spinel_pack_uint!(InterfaceType);
impl_spinel_unpack_uint!(InterfaceType);

impl std::fmt::Display for InterfaceType {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(self, f)
    }
}

impl From<InterfaceType> for u32 {
    fn from(x: InterfaceType) -> Self {
        use InterfaceType::*;
        match x {
            Bootloader => 0,
            ZigbeeIp => 2,
            Thread => 3,

            Unknown(x) => x,
        }
    }
}

impl From<u32> for InterfaceType {
    fn from(id: u32) -> Self {
        use InterfaceType::*;
        match id {
            0 => Bootloader,
            2 => ZigbeeIp,
            3 => Thread,
            x => Unknown(x),
        }
    }
}

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum CapIeee802154 {
    Spec2003,
    Spec2006,
    Spec2011,
    Pib,
    Mod2450MhzOqpsk,
    Mod915MhzOqpsk,
    Mod868MhzOqpsk,
    Mod915MhzBpsk,
    Mod868MhzBpsk,
    Mod915MhzAsk,
    Mod868MhzAsk,
    Unknown(u32),
}
impl_sub_enum!(Cap::Ieee802154, CapIeee802154);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum CapConfig {
    Ftd,
    Mtd,
    Radio,
    Unknown(u32),
}
impl_sub_enum!(Cap::Config, CapConfig);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum CapRole {
    Router,
    Sleepy,
    Unknown(u32),
}
impl_sub_enum!(Cap::Role, CapRole);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum CapOt {
    MacWhitelist,
    MacRaw,
    OobSteeringData,
    ChannelMonitor,
    ErrorRateTracking,
    ChannelManager,
    LogMetadata,
    TimeSync,
    ChildSupervision,
    PosixApp,
    Slaac,
    RadioCoex,
    MacRetryHistogram,
    Unknown(u32),
}
impl_sub_enum!(Cap::Ot, CapOt);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum CapThread {
    Commissioner,
    TmfProxy,
    UdpForward,
    Joiner,
    BorderRouter,
    Service,
    Unknown(u32),
}
impl_sub_enum!(Cap::Thread, CapThread);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum CapNest {
    LegacyInterface,
    LegacyNetWake,
    TransmitHook,
    Unknown(u32),
}
impl_sub_enum!(Cap::Nest, CapNest);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum CapNet {
    Thread(u32, u32),
    Unknown(u32),
}
impl_sub_enum!(Cap::Net, CapNet);

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum Cap {
    Lock,
    NetSave,
    Hbo,
    PowerSave,
    Counters,
    JamDetect,
    PeekPoke,
    WritableRawStream,
    Gpio,
    Trng,
    CmdMulti,
    UnsolUpdateFilter,
    McuPowerState,
    Pcap,
    Ieee802154(CapIeee802154),
    Config(CapConfig),
    Role(CapRole),
    Net(CapNet),
    Ot(CapOt),
    Thread(CapThread),
    Nest(CapNest),
    Vendor(u32),
    Experimental(u32),
    Unknown(u32),
}
impl_spinel_pack_uint!(Cap);
impl_spinel_unpack_uint!(Cap);

impl From<Cap> for u32 {
    fn from(cap: Cap) -> Self {
        use Cap::*;
        match cap {
            Lock => 1,
            NetSave => 2,
            Hbo => 3,
            PowerSave => 4,
            Counters => 5,
            JamDetect => 6,
            PeekPoke => 7,
            WritableRawStream => 8,
            Gpio => 9,
            Trng => 10,
            CmdMulti => 11,
            UnsolUpdateFilter => 12,
            McuPowerState => 13,
            Pcap => 14,

            Ieee802154(CapIeee802154::Spec2003) => 16,
            Ieee802154(CapIeee802154::Spec2006) => 17,
            Ieee802154(CapIeee802154::Spec2011) => 18,
            Ieee802154(CapIeee802154::Pib) => 21,
            Ieee802154(CapIeee802154::Mod2450MhzOqpsk) => 24,
            Ieee802154(CapIeee802154::Mod915MhzOqpsk) => 25,
            Ieee802154(CapIeee802154::Mod868MhzOqpsk) => 26,
            Ieee802154(CapIeee802154::Mod915MhzBpsk) => 27,
            Ieee802154(CapIeee802154::Mod868MhzBpsk) => 28,
            Ieee802154(CapIeee802154::Mod915MhzAsk) => 29,
            Ieee802154(CapIeee802154::Mod868MhzAsk) => 30,

            Ieee802154(CapIeee802154::Unknown(x)) => x,

            Config(CapConfig::Ftd) => 32,
            Config(CapConfig::Mtd) => 33,
            Config(CapConfig::Radio) => 34,
            Config(CapConfig::Unknown(x)) => x,

            Role(CapRole::Router) => 48,
            Role(CapRole::Sleepy) => 49,
            Role(CapRole::Unknown(x)) => x,

            Net(CapNet::Thread(1, 0)) => 52,
            Net(CapNet::Thread(1, _)) => 53,
            Net(CapNet::Thread(_, _)) => 79,
            Net(CapNet::Unknown(x)) => x,

            Ot(CapOt::MacWhitelist) => 512,
            Ot(CapOt::MacRaw) => 513,
            Ot(CapOt::OobSteeringData) => 514,
            Ot(CapOt::ChannelMonitor) => 515,
            Ot(CapOt::ErrorRateTracking) => 516,
            Ot(CapOt::ChannelManager) => 517,
            Ot(CapOt::LogMetadata) => 518,
            Ot(CapOt::TimeSync) => 519,
            Ot(CapOt::ChildSupervision) => 520,
            Ot(CapOt::PosixApp) => 521,
            Ot(CapOt::Slaac) => 522,
            Ot(CapOt::RadioCoex) => 523,
            Ot(CapOt::MacRetryHistogram) => 524,
            Ot(CapOt::Unknown(x)) => x,

            Thread(CapThread::Commissioner) => 1024,
            Thread(CapThread::TmfProxy) => 1025,
            Thread(CapThread::UdpForward) => 1026,
            Thread(CapThread::Joiner) => 1027,
            Thread(CapThread::BorderRouter) => 1028,
            Thread(CapThread::Service) => 1029,
            Thread(CapThread::Unknown(x)) => x,

            Nest(CapNest::LegacyInterface) => 15296,
            Nest(CapNest::LegacyNetWake) => 15297,
            Nest(CapNest::TransmitHook) => 15298,
            Nest(CapNest::Unknown(x)) => x,

            Vendor(x) => x,
            Experimental(x) => x,
            Unknown(x) => x,
        }
    }
}

impl From<u32> for Cap {
    fn from(id: u32) -> Self {
        use Cap::*;
        match id {
            1 => Lock,
            2 => NetSave,
            3 => Hbo,
            4 => PowerSave,
            5 => Counters,
            6 => JamDetect,
            7 => PeekPoke,
            8 => WritableRawStream,
            9 => Gpio,
            10 => Trng,
            11 => CmdMulti,
            12 => UnsolUpdateFilter,
            13 => McuPowerState,
            14 => Pcap,

            16 => Ieee802154(CapIeee802154::Spec2003),
            17 => Ieee802154(CapIeee802154::Spec2006),
            18 => Ieee802154(CapIeee802154::Spec2011),
            21 => Ieee802154(CapIeee802154::Pib),
            24 => Ieee802154(CapIeee802154::Mod2450MhzOqpsk),
            25 => Ieee802154(CapIeee802154::Mod915MhzOqpsk),
            26 => Ieee802154(CapIeee802154::Mod868MhzOqpsk),
            27 => Ieee802154(CapIeee802154::Mod915MhzBpsk),
            28 => Ieee802154(CapIeee802154::Mod868MhzBpsk),
            29 => Ieee802154(CapIeee802154::Mod915MhzAsk),
            30 => Ieee802154(CapIeee802154::Mod868MhzAsk),
            x if x >= 0x10 && x < 0x20 => Ieee802154(CapIeee802154::Unknown(x)),

            32 => Config(CapConfig::Ftd),
            33 => Config(CapConfig::Mtd),
            34 => Config(CapConfig::Radio),
            x if x >= 0x20 && x < 0x30 => Config(CapConfig::Unknown(x)),

            48 => Role(CapRole::Router),
            49 => Role(CapRole::Sleepy),
            x if x >= 0x30 && x < 0x40 => Role(CapRole::Unknown(x)),

            52 => Net(CapNet::Thread(1, 0)),
            53 => Net(CapNet::Thread(1, 1)),
            x if x >= 0x40 && x < 0x50 => Net(CapNet::Unknown(x)),

            512 => Ot(CapOt::MacWhitelist),
            513 => Ot(CapOt::MacRaw),
            514 => Ot(CapOt::OobSteeringData),
            515 => Ot(CapOt::ChannelMonitor),
            516 => Ot(CapOt::ErrorRateTracking),
            517 => Ot(CapOt::ChannelManager),
            518 => Ot(CapOt::LogMetadata),
            519 => Ot(CapOt::TimeSync),
            520 => Ot(CapOt::ChildSupervision),
            521 => Ot(CapOt::PosixApp),
            522 => Ot(CapOt::Slaac),
            523 => Ot(CapOt::RadioCoex),
            524 => Ot(CapOt::MacRetryHistogram),
            x if x >= 512 && x < 640 => Ot(CapOt::Unknown(x)),

            1024 => Thread(CapThread::Commissioner),
            1025 => Thread(CapThread::TmfProxy),
            1026 => Thread(CapThread::UdpForward),
            1027 => Thread(CapThread::Joiner),
            1028 => Thread(CapThread::BorderRouter),
            1029 => Thread(CapThread::Service),
            x if x >= 1024 && x < 1152 => Thread(CapThread::Unknown(x)),

            15296 => Nest(CapNest::LegacyInterface),
            15297 => Nest(CapNest::LegacyNetWake),
            15298 => Nest(CapNest::TransmitHook),
            x if x >= 15296 && x < 15360 => Nest(CapNest::Unknown(x)),

            x if x >= 15360 && x < 16384 => Vendor(x),
            x if x >= 2000000 && x < 2097152 => Experimental(x),

            x => Unknown(x),
        }
    }
}

#[derive(Debug, Clone, Copy, Eq, PartialEq, Hash, Ord, PartialOrd)]
pub enum ScanState {
    Idle,
    Beacon,
    Energy,
    Discover,
    Unknown(u32),
}
impl_spinel_pack_uint!(ScanState);
impl_spinel_unpack_uint!(ScanState);

impl std::fmt::Display for ScanState {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        std::fmt::Debug::fmt(self, f)
    }
}

impl From<ScanState> for u32 {
    fn from(state: ScanState) -> Self {
        match state {
            ScanState::Idle => 0,
            ScanState::Beacon => 1,
            ScanState::Energy => 2,
            ScanState::Discover => 3,

            ScanState::Unknown(x) => x,
        }
    }
}

impl From<u32> for ScanState {
    fn from(id: u32) -> Self {
        match id {
            0 => ScanState::Idle,
            1 => ScanState::Beacon,
            2 => ScanState::Energy,
            3 => ScanState::Discover,
            x => ScanState::Unknown(x),
        }
    }
}
