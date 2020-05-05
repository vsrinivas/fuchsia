// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! LoWPAN Spinel Driver

#![warn(rust_2018_idioms)]
#![warn(clippy::all)]

// NOTE: This line is a hack to work around some issues
//       with respect to external rust crates.
use spinel_pack::{self as spinel_pack};

mod driver;
mod flow_window;
mod spinel;

#[cfg(test)]
#[macro_export]
macro_rules! traceln (($($args:tt)*) => { eprintln!($($args)*); }; );

#[cfg(not(test))]
#[macro_export]
macro_rules! traceln (($($args:tt)*) => { }; );

#[macro_use]
mod prelude {
    pub use traceln;

    pub use anyhow::{format_err, Context as _};
    pub use fasync::TimeoutExt as _;
    pub use fuchsia_async as fasync;
    pub use fuchsia_syslog::macros::*;
    pub use fuchsia_zircon_status::Status as ZxStatus;
    pub use futures::prelude::*;
    pub use spinel_pack::prelude::*;
}

use crate::prelude::*;

use crate::driver::SpinelDriver;
use anyhow::Error;
use fidl_fuchsia_lowpan_device::{RegisterMarker, RegisterProxyInterface};
use fidl_fuchsia_lowpan_spinel::{
    DeviceMarker as SpinelDeviceMarker, DeviceProxy as SpinelDeviceProxy,
};
use fuchsia_component::client::connect_to_service_at;
use lowpan_driver_common::register_and_serve_driver;

/// This struct contains the arguments decoded from the command
/// line invocation of the driver.
#[derive(argh::FromArgs, PartialEq, Debug)]
struct DriverArgs {
    #[argh(
        option,
        long = "serviceprefix",
        description = "service namespace prefix (default: '/svc')",
        default = "\"/svc\".to_string()"
    )]
    pub service_prefix: String,

    #[argh(
        option,
        long = "spinelprefix",
        description = "spinel namespace prefix (default: '/svc')",
        default = "\"/svc\".to_string()"
    )]
    pub spinel_prefix: String,

    #[argh(
        option,
        long = "name",
        description = "name of interface",
        default = "\"lowpan0\".to_string()"
    )]
    pub name: String,
}

async fn run_driver<N, RP>(
    name: N,
    registry: RP,
    spinel_device_proxy: SpinelDeviceProxy,
) -> Result<(), Error>
where
    N: AsRef<str>,
    RP: RegisterProxyInterface,
{
    let driver = SpinelDriver::from(spinel_device_proxy);

    let lowpan_device_task =
        register_and_serve_driver(name.as_ref(), registry, &driver).boxed_local();

    fx_log_info!("Registered Spinel LoWPAN device {:?}", name.as_ref());

    let driver_stream = driver.take_inbound_stream().try_collect::<()>();
    //        driver.take_inbound_stream().try_for_each(|_| futures::future::ready(Ok(())));

    // All three of these tasks will run indefinitely
    // as long as there are no irrecoverable problems.
    //
    // And, yes, strangely the parenthesis seem
    // necessary, rustc complains about the `?` without
    // them.
    (futures::select! {
        ret = driver_stream.fuse() => ret,
        ret = lowpan_device_task.fuse() => ret,
    })?;

    fx_log_info!("Spinel LoWPAN device {:?} has shutdown.", name.as_ref());

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: DriverArgs = argh::from_env();

    fuchsia_syslog::init_with_tags(&["lowpan-spinel-driver"]).context("initialize logging")?;

    run_driver(
        args.name,
        connect_to_service_at::<RegisterMarker>(args.service_prefix.as_str())
            .context("Failed to connect to Lowpan Registry service")?,
        connect_to_service_at::<SpinelDeviceMarker>(args.spinel_prefix.as_str())
            .context("Failed to connect to Spinel device")?,
    )
    .await
}
