// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "1024"]

mod access_point;
mod client;
mod config_management;
mod legacy;
mod mode_management;
mod regulatory_manager;
mod telemetry;
mod util;

use {
    crate::{
        client::network_selection::NetworkSelector,
        config_management::{SavedNetworksManager, SavedNetworksManagerApi},
        legacy::{device, IfaceRef},
        mode_management::{
            create_iface_manager, iface_manager_api::IfaceManagerApi, phy_manager::PhyManager,
        },
        regulatory_manager::RegulatoryManager,
        telemetry::serve_telemetry,
    },
    anyhow::{format_err, Context as _, Error},
    fidl::{endpoints::RequestStream, handle::AsyncChannel},
    fidl_fuchsia_location_namedplace::RegulatoryRegionWatcherMarker,
    fidl_fuchsia_process_lifecycle::{LifecycleRequest, LifecycleRequestStream},
    fidl_fuchsia_wlan_device_service::{DeviceMonitorMarker, DeviceServiceMarker},
    fidl_fuchsia_wlan_policy as fidl_policy, fuchsia_async as fasync,
    fuchsia_async::DurationExt,
    fuchsia_cobalt::{CobaltConnector, ConnectionType},
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::component,
    fuchsia_runtime::{self as fruntime, HandleInfo, HandleType},
    fuchsia_syslog as syslog,
    fuchsia_zircon::{self as zx, prelude::*},
    futures::{
        self,
        channel::{mpsc, oneshot},
        future::{try_join, BoxFuture},
        lock::Mutex,
        prelude::*,
        select, TryFutureExt,
    },
    log::{error, info, warn},
    pin_utils::pin_mut,
    std::{process, sync::Arc},
    void::Void,
    wlan_metrics_registry::{self as metrics},
};

const REGULATORY_LISTENER_TIMEOUT_SEC: i64 = 30;

async fn serve_fidl(
    ap: access_point::AccessPoint,
    configurator: legacy::deprecated_configuration::DeprecatedConfigurator,
    iface_manager: Arc<Mutex<dyn IfaceManagerApi + Send>>,
    legacy_client_ref: IfaceRef,
    saved_networks: Arc<dyn SavedNetworksManagerApi>,
    network_selector: Arc<NetworkSelector>,
    client_sender: util::listener::ClientListenerMessageSender,
    client_listener_msgs: mpsc::UnboundedReceiver<util::listener::ClientListenerMessage>,
    ap_listener_msgs: mpsc::UnboundedReceiver<util::listener::ApMessage>,
    regulatory_receiver: oneshot::Receiver<()>,
) -> Result<Void, Error> {
    // Wait a bit for the country code to be set before serving the policy APIs.
    let regulatory_listener_timeout =
        fasync::Timer::new(REGULATORY_LISTENER_TIMEOUT_SEC.seconds().after_now());
    select! {
        _ = regulatory_listener_timeout.fuse() => {
            info!(
                "Country code was not set after {} seconds.  Proceeding to serve policy API.",
                REGULATORY_LISTENER_TIMEOUT_SEC,
            );
        },
        result = regulatory_receiver.fuse() => {
            match result {
                Ok(()) => {
                    info!("Country code has been set.  Proceeding to serve policy API.");
                },
                Err(e) => info!("Waiting for initial country code failed: {:?}", e),
            }
        }
    }

    let mut fs = ServiceFs::new();

    inspect_runtime::serve(component::inspector(), &mut fs)?;

    let client_sender1 = client_sender.clone();
    let client_sender2 = client_sender.clone();

    let second_ap = ap.clone();

    let saved_networks_clone = saved_networks.clone();

    let client_provider_lock = Arc::new(Mutex::new(()));

    // TODO(sakuma): Once the legacy API is deprecated, the interface manager should default to
    // stopped.
    {
        let mut iface_manager = iface_manager.lock().await;
        iface_manager.start_client_connections().await?;
    }

    let _ = fs
        .dir("svc")
        .add_fidl_service(move |reqs| {
            fasync::Task::spawn(client::serve_provider_requests(
                iface_manager.clone(),
                client_sender1.clone(),
                Arc::clone(&saved_networks_clone),
                Arc::clone(&network_selector),
                client_provider_lock.clone(),
                reqs,
            ))
            .detach()
        })
        .add_fidl_service(move |reqs| {
            fasync::Task::spawn(client::serve_listener_requests(client_sender2.clone(), reqs))
                .detach()
        })
        .add_fidl_service(move |reqs| {
            fasync::Task::spawn(ap.clone().serve_provider_requests(reqs)).detach()
        })
        .add_fidl_service(move |reqs| {
            fasync::Task::spawn(second_ap.clone().serve_listener_requests(reqs)).detach()
        })
        .add_fidl_service(move |reqs| {
            fasync::Task::spawn(configurator.clone().serve_deprecated_configuration(reqs)).detach()
        })
        .add_fidl_service(|reqs| {
            let fut =
                legacy::deprecated_client::serve_deprecated_client(reqs, legacy_client_ref.clone())
                    .unwrap_or_else(|e| error!("error serving deprecated client API: {}", e));
            fasync::Task::spawn(fut).detach()
        });
    let service_fut = fs.take_and_serve_directory_handle()?.collect::<()>().fuse();
    pin_mut!(service_fut);

    let serve_client_policy_listeners = util::listener::serve::<
        fidl_policy::ClientStateUpdatesProxy,
        fidl_policy::ClientStateSummary,
        util::listener::ClientStateUpdate,
    >(client_listener_msgs)
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

/// Calls the metric recording function immediately and every 24 hours.
async fn saved_networks_manager_metrics_loop(saved_networks: Arc<dyn SavedNetworksManagerApi>) {
    loop {
        saved_networks.record_periodic_metrics().await;
        fasync::Timer::new(24.hours().after_now()).await;
    }
}

/// Runs the recording and sending of metrics to Cobalt.
async fn serve_metrics(
    saved_networks: Arc<dyn SavedNetworksManagerApi>,
    cobalt_fut: impl Future<Output = ()>,
) -> Result<(), Error> {
    let record_metrics_fut = saved_networks_manager_metrics_loop(saved_networks);
    try_join(record_metrics_fut.map(|()| Ok(())), cobalt_fut.map(|()| Ok(()))).await.map(|_| ())
}

// Some builds will not include the RegulatoryRegionWatcher.  In such cases, wlancfg can continue
// to run, though it will not be able to set its country code and will fallback to world wide.
fn run_regulatory_manager(
    iface_manager: Arc<Mutex<dyn IfaceManagerApi + Send>>,
    regulatory_sender: oneshot::Sender<()>,
) -> BoxFuture<'static, Result<(), Error>> {
    match fuchsia_component::client::connect_to_protocol::<RegulatoryRegionWatcherMarker>() {
        Ok(regulatory_svc) => {
            let regulatory_manager = RegulatoryManager::new(regulatory_svc, iface_manager);
            let regulatory_fut = async move {
                regulatory_manager.run(regulatory_sender).await.unwrap_or_else(|e| {
                    error!("regulatory manager failed: {:?}", e);
                });
                Ok(())
            };

            Box::pin(regulatory_fut)
        }
        Err(e) => {
            error!("could not connect to regulatory manager: {:?}", e);
            let regulatory_fut = async move { Ok(()) };
            Box::pin(regulatory_fut)
        }
    }
}

// This lifecycle handler is required to make use of the "main_process_critical" functionality
// in Components V2. It ensures that our process exits cleanly when the system begins a shutdown.
// If the process were to exit with a non-zero exit code, the system would abruptly reboot rather
// than performing a clean shutdown.
async fn run_lifecycle_handler() -> Result<(), Error> {
    match fruntime::take_startup_handle(HandleInfo::new(HandleType::Lifecycle, 0)) {
        Some(lifecycle_handle) => {
            let x: zx::Channel = lifecycle_handle.into();
            let async_x = AsyncChannel::from(
                fasync::Channel::from_channel(x).expect("Async channel conversion failed."),
            );
            let mut req_stream = LifecycleRequestStream::from_channel(async_x);
            while let Some(request) =
                req_stream.try_next().await.expect("Failure receiving lifecycle FIDL message")
            {
                match request {
                    LifecycleRequest::Stop { control_handle: _c } => {
                        info!("Receive lifecycle request to stop.");
                        process::exit(0);
                    }
                }
            }

            // We only arrive here if the lifecycle channel closed without
            // first sending the shutdown event, which is unexpected.
            error!("Lifecycle channel closed without first sending shutdown event");
            process::abort();
        }
        None => {
            warn!("No lifecycle channel received, likely running as a v1 component.");
            Ok(())
        }
    }
}

async fn run_all_futures() -> Result<(), Error> {
    let wlan_svc = fuchsia_component::client::connect_to_protocol::<DeviceServiceMarker>()
        .context("failed to connect to device service")?;
    let monitor_svc = fuchsia_component::client::connect_to_protocol::<DeviceMonitorMarker>()
        .context("failed to connect to device monitor")?;
    let (cobalt_api, cobalt_fut) =
        CobaltConnector::default().serve(ConnectionType::project_id(metrics::PROJECT_ID));

    let (telemetry_sender, telemetry_fut) =
        serve_telemetry(component::inspector().root().create_child("client_stats"));

    let saved_networks = Arc::new(SavedNetworksManager::new(cobalt_api.clone()).await?);
    let network_selector = Arc::new(NetworkSelector::new(
        saved_networks.clone(),
        cobalt_api.clone(),
        component::inspector().root().create_child("network_selector"),
        telemetry_sender.clone(),
    ));

    let phy_manager = Arc::new(Mutex::new(PhyManager::new(
        wlan_svc.clone(),
        monitor_svc.clone(),
        component::inspector().root().create_child("phy_manager"),
    )));
    let configurator =
        legacy::deprecated_configuration::DeprecatedConfigurator::new(phy_manager.clone());

    let (watcher_proxy, watcher_server_end) = fidl::endpoints::create_proxy()?;
    monitor_svc.watch_devices(watcher_server_end)?;

    let (client_sender, client_receiver) = mpsc::unbounded();
    let (ap_sender, ap_receiver) = mpsc::unbounded();
    let (iface_manager, iface_manager_service) = create_iface_manager(
        phy_manager.clone(),
        client_sender.clone(),
        ap_sender.clone(),
        wlan_svc.clone(),
        saved_networks.clone(),
        network_selector.clone(),
        cobalt_api.clone(),
        telemetry_sender.clone(),
    );

    let legacy_client = IfaceRef::new();
    let listener = device::Listener::new(
        wlan_svc.clone(),
        legacy_client.clone(),
        phy_manager.clone(),
        iface_manager.clone(),
    );

    let (regulatory_sender, regulatory_receiver) = oneshot::channel();
    let ap =
        access_point::AccessPoint::new(iface_manager.clone(), ap_sender, Arc::new(Mutex::new(())));
    let fidl_fut = serve_fidl(
        ap,
        configurator,
        iface_manager.clone(),
        legacy_client,
        saved_networks.clone(),
        network_selector,
        client_sender,
        client_receiver,
        ap_receiver,
        regulatory_receiver,
    );

    let dev_watcher_fut = watcher_proxy
        .take_event_stream()
        .try_for_each(|evt| device::handle_event(&listener, evt).map(Ok))
        .err_into()
        .and_then(|_| {
            let result: Result<(), Error> =
                Err(format_err!("Device watcher future exited unexpectedly"));
            future::ready(result)
        });

    let metrics_fut = serve_metrics(saved_networks.clone(), cobalt_fut);
    let regulatory_fut = run_regulatory_manager(iface_manager.clone(), regulatory_sender);
    let lifecycle_fut = run_lifecycle_handler();

    let _ = futures::try_join!(
        fidl_fut,
        dev_watcher_fut,
        iface_manager_service,
        metrics_fut,
        regulatory_fut,
        lifecycle_fut,
        telemetry_fut.map(Ok),
    )?;
    Ok(())
}

// The return value from main() gets swallowed, including if it returns a Result<Err>. Therefore,
// use this simple wrapper to ensure that any errors from run_all_futures() are printed to the log.
#[fasync::run_singlethreaded]
async fn main() {
    // Initialize logging with a tag that can be used to select these logs for forwarding to console
    syslog::init_with_tags(&["wlan"]).expect("Syslog init should not fail");
    if let Err(e) = run_all_futures().await {
        error!("{:?}", e);
    }
}
