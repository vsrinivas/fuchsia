// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! LoWPAN Dummy Driver

use anyhow::{Context as _, Error};
use fidl_fuchsia_factory_lowpan::{FactoryRegisterMarker, FactoryRegisterProxyInterface};
use fidl_fuchsia_lowpan_device::{RegisterMarker, RegisterProxyInterface};
use fuchsia_async as fasync;
use fuchsia_component::client::connect_to_protocol;
use fuchsia_syslog::macros::*;
use futures::prelude::*;
use lowpan_driver_common::{
    register_and_serve_driver, register_and_serve_driver_factory, DummyDevice,
};
use std::default::Default;

async fn run_driver<N, RP, RFP>(
    name: N,
    registry: RP,
    factory_registry: Option<RFP>,
    driver: DummyDevice,
) -> Result<(), Error>
where
    N: AsRef<str>,
    RP: RegisterProxyInterface,
    RFP: FactoryRegisterProxyInterface,
{
    let name = name.as_ref();
    let driver_ref = &driver;

    let lowpan_device_task = register_and_serve_driver(name, registry, driver_ref).boxed_local();

    fx_log_info!("Registered Dummy LoWPAN device {:?}", name);

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
    .boxed_local();

    // All three of these tasks will run indefinitely
    // as long as there are no irrecoverable problems.
    //
    // And, yes, strangely the parenthesis seem
    // necessary, rustc complains about the `?` without
    // them.
    (futures::select! {
        ret = lowpan_device_task.fuse() => ret,
        _ = lowpan_device_factory_task.fuse() => unreachable!(),
    })?;

    fx_log_info!("Dummy LoWPAN device {:?} has shutdown.", name);

    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["lowpan-dummy-driver"]).context("initialize logging")?;

    let name = "lowpan0";

    let device = DummyDevice::default();

    fx_log_info!("Connecting to LoWPAN service");

    run_driver(
        name,
        connect_to_protocol::<RegisterMarker>()
            .context("Failed to connect to Lowpan Registry service")?,
        connect_to_protocol::<FactoryRegisterMarker>().ok(),
        device,
    )
    .inspect(|_| fx_log_info!("Dummy LoWPAN device {:?} has shutdown.", name))
    .await
}
