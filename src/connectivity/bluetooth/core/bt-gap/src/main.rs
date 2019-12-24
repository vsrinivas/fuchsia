// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Macros used to serialize bonding data FIDL types for persistent storage.
#[macro_use]
extern crate serde_derive;

use {
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_bluetooth::Appearance,
    fidl_fuchsia_bluetooth_bredr::ProfileMarker,
    fidl_fuchsia_bluetooth_control::ControlRequestStream,
    fidl_fuchsia_bluetooth_gatt::Server_Marker,
    fidl_fuchsia_bluetooth_le::{CentralMarker, PeripheralMarker},
    fidl_fuchsia_device::{NameProviderMarker, DEFAULT_DEVICE_NAME},
    fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_service, server::ServiceFs},
    fuchsia_syslog::{self as syslog, fx_log_err, fx_log_info, fx_log_warn},
    futures::{channel::mpsc, future::try_join3, FutureExt, StreamExt, TryFutureExt, TryStreamExt},
    pin_utils::pin_mut,
};

use crate::{
    adapters::{AdapterEvent::*, *},
    generic_access_service::GenericAccessService,
    host_dispatcher::{HostService::*, *},
};

mod adapters;
mod generic_access_service;
mod host_device;
mod host_dispatcher;
mod services;
mod store;
#[cfg(test)]
mod test;
mod types;

const BT_GAP_COMPONENT_ID: &'static str = "bt-gap";

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["bt-gap"]).expect("Can't init logger");
    fx_log_info!("Starting bt-gap...");
    let result = run().await.context("Error running BT-GAP");
    if let Err(e) = &result {
        fx_log_err!("{:?}", e)
    };
    Ok(result?)
}

// Returns the device host name that we assign as the local Bluetooth device name by default.
async fn get_host_name() -> Result<String, Error> {
    // Obtain the local device name to assign it as the default Bluetooth name,
    let name_provider = connect_to_service::<NameProviderMarker>()?;
    name_provider
        .get_device_name()
        .await?
        .map_err(|e| format_err!("failed to obtain host name: {:?}", e))
}

async fn run() -> Result<(), Error> {
    let inspect = fuchsia_inspect::Inspector::new();
    let stash_inspect = inspect.root().create_child("persistent");
    let stash = store::stash::init_stash(BT_GAP_COMPONENT_ID, stash_inspect)
        .await
        .context("Error initializing Stash service")?;

    let local_name = get_host_name().await.unwrap_or(DEFAULT_DEVICE_NAME.to_string());
    let (gas_channel_sender, generic_access_req_stream) = mpsc::channel(0);
    let hd = HostDispatcher::new(
        local_name,
        Appearance::Display,
        stash,
        inspect.root().create_child("system"),
        gas_channel_sender,
    );
    let watch_hd = hd.clone();
    let central_hd = hd.clone();
    let control_hd = hd.clone();
    let peripheral_hd = hd.clone();
    let profile_hd = hd.clone();
    let gatt_hd = hd.clone();
    let bootstrap_hd = hd.clone();

    let host_watcher_task = async {
        let stream = watch_hosts();
        pin_mut!(stream);
        while let Some(msg) = stream.try_next().await? {
            let hd = watch_hd.clone();
            match msg {
                AdapterAdded(device_path) => {
                    let result = hd.add_adapter(&device_path).await;
                    if let Err(e) = &result {
                        fx_log_warn!("Error adding bt-host device '{:?}': {:?}", device_path, e);
                    }
                    result?
                }
                AdapterRemoved(device_path) => {
                    hd.rm_adapter(&device_path);
                }
            }
        }
        Ok(())
    };

    let generic_access_service_task =
        GenericAccessService { hd: hd.clone(), generic_access_req_stream }.run().map(|()| Ok(()));

    let mut fs = ServiceFs::new();
    inspect.serve(&mut fs)?;
    fs.dir("svc")
        .add_fidl_service(move |s| control_service(control_hd.clone(), s))
        .add_service_at(CentralMarker::NAME, move |chan| {
            if let Ok(chan) = fasync::Channel::from_channel(chan) {
                fx_log_info!("Connecting CentralService to Adapter");
                fasync::spawn(central_hd.clone().request_host_service(chan, LeCentral));
            }
            None
        })
        .add_service_at(PeripheralMarker::NAME, move |chan| {
            if let Ok(chan) = fasync::Channel::from_channel(chan) {
                fx_log_info!("Connecting Peripheral Service to Adapter");
                fasync::spawn(peripheral_hd.clone().request_host_service(chan, LePeripheral));
            }
            None
        })
        .add_service_at(ProfileMarker::NAME, move |chan| {
            if let Ok(chan) = fasync::Channel::from_channel(chan) {
                fx_log_info!("Connecting Profile Service to Adapter");
                fasync::spawn(profile_hd.clone().request_host_service(chan, Profile));
            }
            None
        })
        .add_service_at(Server_Marker::NAME, move |chan| {
            if let Ok(chan) = fasync::Channel::from_channel(chan) {
                fx_log_info!("Connecting Gatt Service to Adapter");
                fasync::spawn(gatt_hd.clone().request_host_service(chan, LeGatt));
            }
            None
        })
        // TODO(1496) - according fuchsia.bluetooth.sys/bootstrap.fidl, the bootstrap service should
        // only be available before initialization, and only allow a single commit before becoming
        // unservicable. This behavior interacts with parts of Bluetooth lifecycle and component
        // framework design that are not yet complete. For now, we provide the service to whomever
        // asks, whenever, but clients should not rely on this. The implementation will change once
        // we have a better solution.
        .add_fidl_service(move |request_stream| {
            fx_log_info!("Serving Bootstrap Service");
            fasync::spawn(
                services::bootstrap::run(bootstrap_hd.clone(), request_stream)
                    .unwrap_or_else(|e| fx_log_warn!("Bootstrap service failed: {:?}", e)),
            );
        });
    fs.take_and_serve_directory_handle()?;
    let svc_fs_task = fs.collect::<()>().map(Ok);
    try_join3(svc_fs_task, host_watcher_task, generic_access_service_task)
        .await
        .map(|((), (), ())| ())
}

fn control_service(hd: HostDispatcher, stream: ControlRequestStream) {
    fx_log_info!("Spawning Control Service");
    fasync::spawn(
        services::start_control_service(hd.clone(), stream)
            .unwrap_or_else(|e| eprintln!("Failed to spawn {:?}", e)),
    )
}
