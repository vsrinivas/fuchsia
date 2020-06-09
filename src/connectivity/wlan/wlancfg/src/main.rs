// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "1024"]

mod access_point;
mod client;
mod config_management;
mod legacy;
mod mode_management;
mod util;

use {
    crate::{config_management::SavedNetworksManager, mode_management::phy_manager::PhyManager},
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_wlan_device_service::DeviceServiceMarker,
    fidl_fuchsia_wlan_policy as fidl_policy, fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{channel::mpsc, future::try_join, lock::Mutex, prelude::*, select, TryFutureExt},
    log::error,
    pin_utils::pin_mut,
    std::sync::Arc,
    void::Void,
};

async fn serve_fidl(
    client_ref: client::ClientPtr,
    mut ap: access_point::AccessPoint,
    legacy_client_ref: legacy::shim::ClientRef,
    configurator: legacy::deprecated_configuration::DeprecatedConfigurator,
    saved_networks: Arc<SavedNetworksManager>,
) -> Result<Void, Error> {
    let mut fs = ServiceFs::new();
    let (client_sender, listener_msgs) = mpsc::unbounded();
    let client_sender1 = client_sender.clone();
    let client_sender2 = client_sender.clone();

    let (ap_sender, ap_listener_msgs) = mpsc::unbounded();
    ap.set_update_sender(ap_sender);
    let second_ap = ap.clone();

    let saved_networks_clone = Arc::clone(&saved_networks);
    fs.dir("svc")
        .add_fidl_service(|stream| {
            let fut = legacy::shim::serve_legacy(
                stream,
                legacy_client_ref.clone(),
                Arc::clone(&saved_networks),
            )
            .unwrap_or_else(|e| error!("error serving legacy wlan API: {}", e));
            fasync::spawn(fut)
        })
        .add_fidl_service(move |reqs| {
            client::spawn_provider_server(
                client_ref.clone(),
                client_sender1.clone(),
                Arc::clone(&saved_networks_clone),
                reqs,
            )
        })
        .add_fidl_service(move |reqs| client::spawn_listener_server(client_sender2.clone(), reqs))
        .add_fidl_service(move |reqs| fasync::spawn(ap.clone().serve_provider_requests(reqs)))
        .add_fidl_service(move |reqs| {
            fasync::spawn(second_ap.clone().serve_listener_requests(reqs))
        })
        .add_fidl_service(move |reqs| {
            fasync::spawn(configurator.clone().serve_deprecated_configuration(reqs))
        });
    fs.take_and_serve_directory_handle()?;
    let service_fut = fs.collect::<()>().fuse();
    pin_mut!(service_fut);

    let serve_client_policy_listeners = util::listener::serve::<
        fidl_policy::ClientStateUpdatesProxy,
        fidl_policy::ClientStateSummary,
        util::listener::ClientStateUpdate,
    >(listener_msgs)
    .fuse();
    pin_mut!(serve_client_policy_listeners);

    let serve_ap_policy_listeners = util::listener::serve::<
        fidl_policy::AccessPointStateUpdatesProxy,
        Vec<fidl_policy::AccessPointState>,
        util::listener::ApStatesUpdate,
    >(ap_listener_msgs)
    .fuse();
    pin_mut!(serve_ap_policy_listeners);

    loop {
        select! {
            _ = service_fut => (),
            _ = serve_client_policy_listeners => (),
            _ = serve_ap_policy_listeners => (),
        }
    }
}

fn main() -> Result<(), Error> {
    util::logger::init();

    let mut executor = fasync::Executor::new().context("error create event loop")?;
    let wlan_svc = fuchsia_component::client::connect_to_service::<DeviceServiceMarker>()
        .context("failed to connect to device service")?;

    let phy_manager = Arc::new(Mutex::new(PhyManager::new(wlan_svc.clone())));
    let saved_networks = Arc::new(executor.run_singlethreaded(SavedNetworksManager::new())?);
    let legacy_client = legacy::shim::ClientRef::new();
    let client = Arc::new(Mutex::new(client::Client::new_empty()));
    let ap = access_point::AccessPoint::new_empty(phy_manager.clone(), wlan_svc.clone());
    let configurator =
        legacy::deprecated_configuration::DeprecatedConfigurator::new(phy_manager.clone());
    let fidl_fut = serve_fidl(
        client.clone(),
        ap,
        legacy_client.clone(),
        configurator,
        Arc::clone(&saved_networks),
    );

    let (watcher_proxy, watcher_server_end) = fidl::endpoints::create_proxy()?;
    wlan_svc.watch_devices(watcher_server_end)?;
    let listener = legacy::device::Listener::new(wlan_svc, legacy_client, phy_manager, client);
    let fut = watcher_proxy
        .take_event_stream()
        .try_for_each(|evt| {
            legacy::device::handle_event(&listener, evt, Arc::clone(&saved_networks)).map(Ok)
        })
        .err_into()
        .and_then(|_| future::ready(Err(format_err!("Device watcher future exited unexpectedly"))));

    executor.run_singlethreaded(try_join(fidl_fut, fut)).map(|_: (Void, Void)| ())
}
