// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! LoWPAN OpenThread Driver
#![warn(rust_2018_idioms)]
#![warn(clippy::all)]

use anyhow::Error;
use fidl_fuchsia_factory_lowpan::{FactoryRegisterMarker, FactoryRegisterProxyInterface};
use fidl_fuchsia_lowpan_device::{RegisterMarker, RegisterProxyInterface};
use fidl_fuchsia_lowpan_spinel::{
    DeviceMarker as SpinelDeviceMarker, DeviceProxy as SpinelDeviceProxy,
    DeviceSetupProxy as SpinelDeviceSetupProxy,
};
use fuchsia_component::client::connect_to_protocol_at;

use lowpan_driver_common::net::*;
use lowpan_driver_common::spinel::SpinelDeviceSink;
use lowpan_driver_common::{register_and_serve_driver, register_and_serve_driver_factory};
use openthread_fuchsia::Platform as OtPlatform;

use config::Config;

use crate::driver::OtDriver;
use crate::prelude::*;
use std::ffi::CString;

mod config;
mod convert_ext;
mod driver;

#[macro_use]
mod prelude {
    #![allow(unused_imports)]

    pub use crate::convert_ext::FromExt as _;
    pub use crate::convert_ext::IntoExt as _;
    pub use crate::Result;
    pub use anyhow::{bail, format_err, Context as _};
    pub use fasync::TimeoutExt as _;
    pub use fidl_fuchsia_net_ext as fnet_ext;
    pub use fuchsia_async as fasync;
    pub use fuchsia_syslog::macros::*;
    pub use fuchsia_zircon as fz;
    pub use fuchsia_zircon_status::Status as ZxStatus;
    pub use futures::future::BoxFuture;
    pub use futures::stream::BoxStream;
    pub use log::{debug, error, info, trace, warn};
    pub use lowpan_driver_common::ZxResult;
    pub use net_declare::{fidl_ip, fidl_ip_v6};
    pub use std::convert::TryInto;
    pub use std::fmt::Debug;

    pub use futures::prelude::*;
    pub use openthread::prelude::*;
}

pub type Result<T = (), E = anyhow::Error> = std::result::Result<T, E>;

const MAX_EXPONENTIAL_BACKOFF_DELAY_SEC: i64 = 60;
const RESET_EXPONENTIAL_BACKOFF_TIMER_MIN: i64 = 5;

impl Config {
    async fn open_spinel_device_proxy(&self) -> Result<SpinelDeviceProxy, Error> {
        use std::fs::File;
        use std::path::Path;
        const OT_PROTOCOL_PATH: &str = "/dev/class/ot-radio";

        let mut found_device_path =
            Path::new(self.ot_radio_path.as_deref().unwrap_or(OT_PROTOCOL_PATH)).to_owned();

        // If we are just given a directory, try to infer the full path.
        if found_device_path.is_dir() {
            let ot_radio_dir =
                File::open(found_device_path.clone()).context("opening dir in devmgr")?;

            let directory_proxy = fidl_fuchsia_io::DirectoryProxy::new(
                fuchsia_async::Channel::from_channel(fdio::clone_channel(&ot_radio_dir)?)?,
            );

            let ot_radio_devices = files_async::readdir(&directory_proxy).await?;

            // Should have 1 device that implements OT_RADIO
            if ot_radio_devices.len() != 1 {
                return Err(format_err!(
                    "There are {} devices in {:?}, expecting only one",
                    ot_radio_devices.len(),
                    found_device_path
                ));
            }

            let last_device: &files_async::DirEntry = ot_radio_devices.last().unwrap();

            found_device_path = found_device_path.join(last_device.name.clone());
        }

        fx_log_info!("Attempting to use Spinel RCP at {:?}", found_device_path.to_str().unwrap());

        let file = File::open(found_device_path).context("Error opening Spinel RCP")?;

        let spinel_device_setup_proxy = SpinelDeviceSetupProxy::new(fasync::Channel::from_channel(
            fdio::clone_channel(&file)?,
        )?);

        let (client_side, server_side) = fidl::endpoints::create_endpoints::<SpinelDeviceMarker>()?;

        spinel_device_setup_proxy
            .set_channel(server_side)
            .await?
            .map_err(ZxStatus::from_raw)
            .context(
            "Unable to set server-side FIDL channel via spinel_device_setup_proxy.set_channel()",
        )?;

        Ok(client_side.into_proxy()?)
    }

    fn get_backbone_netif_index(&self) -> Option<ot::NetifIndex> {
        if self.backbone_name.is_empty() {
            info!("Backbone interface is disabled");
            return None;
        }

        let c_name = CString::new(self.backbone_name.as_bytes().to_vec())
            .expect("Invalid backbone interface name");

        // SAFETY: Calling `if_name_toindex` is safe assuming that the C-string pointer
        //         being passed into it is valid, which is guaranteed by `CString::as_ptr()`.
        let index = unsafe { libc::if_nametoindex(c_name.as_ptr()) };

        if index == 0 {
            error!("Unable to look up index of interface {:?}", self.backbone_name);
            return None;
        }

        info!("Backbone interface is {:?} (index {})", self.backbone_name, index);

        Some(index)
    }

    /// Async method which returns the future that runs the driver.
    async fn prepare_to_run(&self) -> Result<impl Future<Output = Result<(), Error>>, Error> {
        let spinel_device_proxy = self.open_spinel_device_proxy().await?;
        fx_log_debug!("Spinel device proxy initialized");

        let spinel_sink = SpinelDeviceSink::new(spinel_device_proxy);
        let spinel_stream = spinel_sink.take_stream();

        let netif = TunNetworkInterface::try_new(Some(self.name.clone()))
            .await
            .context("Unable to start TUN driver")?;

        let mut builder = OtPlatform::build().thread_netif_index(
            netif
                .get_index()
                .try_into()
                .expect("Network interface index is too large for OpenThread"),
        );

        // TODO (fxbug.dev/94129): Handle removal/re-add of backbone interface device
        // This is needed for now due to the removal of backbone when its region code is set.
        info!("Wait for 20s before getting backbone interface {:?}", self.backbone_name);
        std::thread::sleep(std::time::Duration::from_secs(20));

        let backbone_netif_index = self.get_backbone_netif_index();

        if let Some(index) = backbone_netif_index {
            builder = builder.backbone_netif_index(index);
        }

        let ot_instance = ot::Instance::new(builder.init(spinel_sink, spinel_stream));

        if let Some(index) = backbone_netif_index {
            ot_instance
                .border_routing_init(index, true)
                .context("Unable to initialize OpenThread border routing")?;
        }

        let driver_future = run_driver(
            self.name.clone(),
            connect_to_protocol_at::<RegisterMarker>(self.service_prefix.as_str())
                .context("Failed to connect to Lowpan Registry service")?,
            connect_to_protocol_at::<FactoryRegisterMarker>(self.service_prefix.as_str()).ok(),
            ot_instance,
            netif,
        );

        Ok(driver_future)
    }
}

async fn run_driver<N, RP, RFP, NI>(
    name: N,
    registry: RP,
    factory_registry: Option<RFP>,
    ot_instance: OtInstanceBox,
    net_if: NI,
) -> Result<(), Error>
where
    N: AsRef<str>,
    RP: RegisterProxyInterface,
    RFP: FactoryRegisterProxyInterface,
    NI: NetworkInterface + Debug,
{
    let name = name.as_ref();
    let driver = OtDriver::new(ot_instance, net_if);
    let driver_ref = &driver;

    let lowpan_device_task = register_and_serve_driver(name, registry, driver_ref).boxed();

    fx_log_info!("Registered OpenThread LoWPAN device {:?}", name);

    let lowpan_device_factory_task = async move {
        if let Some(factory_registry) = factory_registry {
            if let Err(err) =
                register_and_serve_driver_factory(name, factory_registry, driver_ref).await
            {
                fx_log_warn!(
                    "Unable to register and serve factory commands for {:?}: {:?}",
                    name,
                    err
                );
            }
        }

        // If the factory interface throws an error, don't kill the driver;
        // just let the rest keep running.
        futures::future::pending::<Result<(), Error>>().await
    }
    .boxed();

    let driver_stream = driver.main_loop_stream().try_collect::<()>();

    // All three of these tasks will run indefinitely
    // as long as there are no irrecoverable problems.
    //
    // And, yes, strangely the parenthesis seem
    // necessary, rustc complains about the `?` without
    // them.
    (futures::select! {
        ret = driver_stream.fuse() => ret,
        ret = lowpan_device_task.fuse() => ret,
        _ = lowpan_device_factory_task.fuse() => unreachable!(),
    })?;

    fx_log_info!("OpenThread LoWPAN device {:?} has shutdown.", name);

    Ok(())
}

// The OpenThread platform implementation currently requires a multithreaded executor.
#[fasync::run(10)]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["lowpan-ot-driver"]).context("main:initialize_logging")?;

    let config = Config::try_new().context("Config::try_new")?;

    fuchsia_syslog::set_severity(config.log_level);

    let mut attempt_count = 0;

    loop {
        fx_log_info!("Starting LoWPAN OT Driver");

        let driver_future = config
            .prepare_to_run()
            .inspect_err(|e| fx_log_err!("main:prepare_to_run: {:?}", e))
            .await
            .context("main:prepare_to_run")?
            .boxed();

        let start_timestamp = fasync::Time::now();

        let ret = driver_future.await.context("main:driver_task");

        if (fasync::Time::now() - start_timestamp).into_minutes()
            >= RESET_EXPONENTIAL_BACKOFF_TIMER_MIN
        {
            // If the past run has been running for `RESET_EXPONENTIAL_BACKOFF_TIMER_MIN`
            // minutes or longer, then we go ahead and reset the attempt count.
            attempt_count = 0;
        }

        if config.max_auto_restarts <= attempt_count {
            break ret;
        }

        // Implement an exponential backoff for restarts.
        let delay = (1 << attempt_count).min(MAX_EXPONENTIAL_BACKOFF_DELAY_SEC);

        fx_log_err!("Unexpected shutdown: {:?}", ret);
        fx_log_err!("Will attempt to restart in {} seconds.", delay);

        fasync::Timer::new(fasync::Time::after(fz::Duration::from_seconds(delay))).await;

        attempt_count += 1;

        fx_log_info!("Restart attempt {} ({} max)", attempt_count, config.max_auto_restarts);
    }
}
