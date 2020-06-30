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
    DeviceSetupProxy as SpinelDeviceSetupProxy,
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

    #[argh(option, long = "spinelprefix", description = "spinel namespace prefix (ex: '/svc')")]
    pub spinel_prefix: Option<String>,

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

async fn connect_to_spinel_device_proxy_hack() -> Result<SpinelDeviceProxy, Error> {
    use {std::fs::File, std::path::Path};
    const OT_PROTOCOL_PATH: &str = "/dev/class/ot-radio";

    let ot_radio_dir = File::open(OT_PROTOCOL_PATH).context("opening dir in devmgr")?;
    let directory_proxy = fidl_fuchsia_io::DirectoryProxy::new(
        fuchsia_async::Channel::from_channel(fdio::clone_channel(&ot_radio_dir)?)?,
    );

    let ot_radio_devices = files_async::readdir(&directory_proxy).await?;

    // Should have 1 device that implements OT_RADIO
    if ot_radio_devices.len() != 1 {
        return Err(format_err!("incorrect device number {}, shuold be 1", ot_radio_devices.len()));
    }

    let last_device: &files_async::DirEntry = ot_radio_devices.last().unwrap();

    let found_device_path = Path::new(OT_PROTOCOL_PATH).join(last_device.name.clone());

    fx_log_info!("device path {} got", found_device_path.to_str().unwrap());

    let file = File::open(found_device_path).context("err opening ot radio device")?;

    let spinel_device_setup_channel = fasync::Channel::from_channel(fdio::clone_channel(&file)?)?;
    let spinel_device_setup_proxy = SpinelDeviceSetupProxy::new(spinel_device_setup_channel);

    let (client_side, server_side) = fidl::endpoints::create_endpoints::<SpinelDeviceMarker>()?;

    spinel_device_setup_proxy
        .set_channel(server_side.into_channel())
        .await?
        .map_err(|x| format_err!("spinel_device_setup.set_channel() returned error {}", x))?;

    Ok(client_side.into_proxy()?)
}

#[allow(unused)]
fn connect_to_spinel_device_proxy(_prefix: &str) -> Result<SpinelDeviceProxy, Error> {
    connect_to_service_at::<SpinelDeviceMarker>(_prefix)
        .context("Failed to connect to Spinel device")
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: DriverArgs = argh::from_env();

    fuchsia_syslog::init_with_tags(&["lowpan-spinel-driver"]).context("initialize logging")?;

    let spinel_device = if let Some(spinel_prefix) = args.spinel_prefix.as_ref() {
        connect_to_spinel_device_proxy(spinel_prefix.as_str())?
    } else {
        connect_to_spinel_device_proxy_hack().await?
    };

    run_driver(
        args.name,
        connect_to_service_at::<RegisterMarker>(args.service_prefix.as_str())
            .context("Failed to connect to Lowpan Registry service")?,
        spinel_device,
    )
    .await
}
