// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_debug_implementations)]
#![warn(rust_2018_idioms)]
#![warn(clippy::all)]

mod async_condition;
mod dummy_device;
mod lowpan_device;
mod register;
mod serve_to;

pub mod net;
pub mod poll_debugger;
pub mod spinel;

#[cfg(test)]
mod tests;

pub use async_condition::*;
pub use dummy_device::DummyDevice;
pub use lowpan_device::Driver;
pub use poll_debugger::*;
pub use register::*;
pub use serve_to::*;

// NOTE: This line is a hack to work around some issues
//       with respect to external rust crates.
use spinel_pack::{self as spinel_pack};

#[macro_export]
macro_rules! traceln (($($args:tt)*) => { fuchsia_syslog::macros::fx_log_trace!($($args)*); }; );

#[macro_use]
pub(crate) mod prelude_internal {
    pub use traceln;

    pub use fidl::prelude::*;
    pub use futures::prelude::*;
    pub use spinel_pack::prelude::*;

    pub use fuchsia_syslog::macros::*;

    pub use crate::poll_debugger::FutureDebugExt as _;
    pub use crate::poll_debugger::StreamDebugExt as _;
    pub use crate::ServeTo as _;
    pub use crate::{ZxResult, ZxStatus};
    pub use anyhow::{format_err, Context as _};
    pub use async_trait::async_trait;
    pub use fasync::TimeoutExt as _;
    pub use fidl_fuchsia_net_ext as fnet_ext;
    pub use fuchsia_async as fasync;

    pub use net_declare::{fidl_ip, fidl_ip_v6};
}

pub mod lowpan_fidl {
    pub use fidl_fuchsia_factory_lowpan::*;
    pub use fidl_fuchsia_lowpan::*;
    pub use fidl_fuchsia_lowpan_device::*;
    pub use fidl_fuchsia_lowpan_experimental::DeviceConnectorMarker as ExperimentalDeviceConnectorMarker;
    pub use fidl_fuchsia_lowpan_experimental::DeviceConnectorRequest as ExperimentalDeviceConnectorRequest;
    pub use fidl_fuchsia_lowpan_experimental::DeviceConnectorRequestStream as ExperimentalDeviceConnectorRequestStream;
    pub use fidl_fuchsia_lowpan_experimental::DeviceExtraConnectorMarker as ExperimentalDeviceExtraConnectorMarker;
    pub use fidl_fuchsia_lowpan_experimental::DeviceExtraConnectorRequest as ExperimentalDeviceExtraConnectorRequest;
    pub use fidl_fuchsia_lowpan_experimental::DeviceExtraConnectorRequestStream as ExperimentalDeviceExtraConnectorRequestStream;
    pub use fidl_fuchsia_lowpan_experimental::DeviceExtraMarker as ExperimentalDeviceExtraMarker;
    pub use fidl_fuchsia_lowpan_experimental::DeviceExtraRequest as ExperimentalDeviceExtraRequest;
    pub use fidl_fuchsia_lowpan_experimental::DeviceExtraRequestStream as ExperimentalDeviceExtraRequestStream;
    pub use fidl_fuchsia_lowpan_experimental::DeviceMarker as ExperimentalDeviceMarker;
    pub use fidl_fuchsia_lowpan_experimental::DeviceRequest as ExperimentalDeviceRequest;
    pub use fidl_fuchsia_lowpan_experimental::DeviceRequestStream as ExperimentalDeviceRequestStream;
    pub use fidl_fuchsia_lowpan_experimental::TelemetryProviderConnectorMarker;
    pub use fidl_fuchsia_lowpan_experimental::TelemetryProviderConnectorRequest;
    pub use fidl_fuchsia_lowpan_experimental::TelemetryProviderConnectorRequestStream;
    pub use fidl_fuchsia_lowpan_experimental::TelemetryProviderMarker;
    pub use fidl_fuchsia_lowpan_experimental::TelemetryProviderRequest;
    pub use fidl_fuchsia_lowpan_experimental::TelemetryProviderRequestStream;
    pub use fidl_fuchsia_lowpan_experimental::{
        BeaconInfo, BeaconInfoStreamMarker, BeaconInfoStreamRequest, ChannelInfo,
        DeviceRouteConnectorMarker, DeviceRouteConnectorRequest, DeviceRouteConnectorRequestStream,
        DeviceRouteExtraConnectorMarker, DeviceRouteExtraConnectorRequest,
        DeviceRouteExtraConnectorRequestStream, DeviceRouteExtraMarker, DeviceRouteExtraRequest,
        DeviceRouteExtraRequestStream, DeviceRouteMarker, DeviceRouteRequest,
        DeviceRouteRequestStream, ExternalRoute, JoinParams, JoinerCommissioningParams,
        LegacyJoiningConnectorMarker, LegacyJoiningConnectorRequest,
        LegacyJoiningConnectorRequestStream, LegacyJoiningMarker, LegacyJoiningRequest,
        LegacyJoiningRequestStream, NetworkScanParameters, OnMeshPrefix, ProvisionError,
        ProvisioningMonitorRequest, ProvisioningProgress, RoutePreference, Telemetry,
    };
    pub use fidl_fuchsia_lowpan_test::*;
    pub use fidl_fuchsia_lowpan_thread::*;
    pub use fidl_fuchsia_net::Ipv6AddressWithPrefix as Ipv6Subnet;
}

pub use fuchsia_zircon_status::Status as ZxStatus;

/// A `Result` that uses `fuchsia_zircon::Status` for the error condition.
pub type ZxResult<T = ()> = Result<T, ZxStatus>;

const MAX_CONCURRENT: usize = 100;
