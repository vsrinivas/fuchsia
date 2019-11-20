// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "256"]

mod client;
mod config;
mod config_manager;
mod device;
mod fuse_pending;
mod known_ess_store;
mod network_config;
mod policy;
mod shim;
mod state_machine;

use {
    crate::{config::Config, known_ess_store::KnownEssStore},
    failure::{format_err, Error, ResultExt},
    fidl_fuchsia_wlan_device_service::DeviceServiceMarker,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    futures::{channel::mpsc, future::try_join, prelude::*, select},
    parking_lot::Mutex,
    pin_utils::pin_mut,
    std::sync::Arc,
    void::Void,
};

async fn serve_fidl(
    _client_ref: shim::ClientRef,
    ess_store: Arc<KnownEssStore>,
) -> Result<Void, Error> {
    let mut fs = ServiceFs::new();
    let (listener_msg_sender, listener_msgs) = mpsc::unbounded();
    let listener_msg_sender1 = listener_msg_sender.clone();
    let listener_msg_sender2 = listener_msg_sender.clone();
    let ess_store_clone = ess_store.clone();
    fs.dir("svc")
        .add_fidl_service(|stream| {
            let fut = shim::serve_legacy(stream, _client_ref.clone(), Arc::clone(&ess_store))
                .unwrap_or_else(|e| eprintln!("error serving legacy wlan API: {}", e));
            fasync::spawn(fut)
        })
        .add_fidl_service(move |reqs| {
            policy::client::spawn_provider_server(
                Arc::new(Mutex::new(policy::client::Client::new_empty())),
                listener_msg_sender1.clone(),
                Arc::clone(&ess_store_clone),
                reqs,
            )
        })
        .add_fidl_service(move |reqs| {
            policy::client::spawn_listener_server(listener_msg_sender2.clone(), reqs)
        });
    fs.take_and_serve_directory_handle()?;
    let service_fut = fs.collect::<()>().fuse();
    pin_mut!(service_fut);

    let serve_policy_listeners = policy::client::listener::serve(listener_msgs).fuse();
    pin_mut!(serve_policy_listeners);

    loop {
        select! {
            _ = service_fut => (),
            _ = serve_policy_listeners => (),
        }
    }
}

fn main() -> Result<(), Error> {
    let cfg = Config::load_from_file()?;

    let mut executor = fasync::Executor::new().context("error create event loop")?;
    let wlan_svc = fuchsia_component::client::connect_to_service::<DeviceServiceMarker>()
        .context("failed to connect to device service")?;

    let ess_store = Arc::new(KnownEssStore::new()?);
    let legacy_client = shim::ClientRef::new();
    let fidl_fut = serve_fidl(legacy_client.clone(), Arc::clone(&ess_store));

    let (watcher_proxy, watcher_server_end) = fidl::endpoints::create_proxy()?;
    wlan_svc.watch_devices(watcher_server_end)?;
    let listener = device::Listener::new(wlan_svc, cfg, legacy_client);
    let fut = watcher_proxy
        .take_event_stream()
        .try_for_each(|evt| device::handle_event(&listener, evt, Arc::clone(&ess_store)).map(Ok))
        .err_into()
        .and_then(|_| future::ready(Err(format_err!("Device watcher future exited unexpectedly"))));

    executor.run_singlethreaded(try_join(fidl_fut, fut)).map(|_: (Void, Void)| ())
}
