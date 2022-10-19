// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]
// The complexity of a separate struct doesn't seem universally better than having many arguments
#![allow(clippy::too_many_arguments)]
#![recursion_limit = "1024"]

use {
    anyhow::{format_err, Context as _, Error},
    fidl_fuchsia_location_namedplace::RegulatoryRegionWatcherMarker,
    fidl_fuchsia_power_clientlevel as fidl_lp, fidl_fuchsia_wlan_common as fidl_common,
    fidl_fuchsia_wlan_device_service::DeviceMonitorMarker,
    fidl_fuchsia_wlan_policy as fidl_policy, fuchsia_async as fasync,
    fuchsia_async::DurationExt,
    fuchsia_component::server::ServiceFs,
    fuchsia_inspect::component,
    fuchsia_inspect_contrib::auto_persist,
    fuchsia_syslog as syslog,
    fuchsia_zircon::prelude::*,
    futures::{
        self,
        channel::{mpsc, oneshot},
        future::OptionFuture,
        lock::Mutex,
        prelude::*,
        select, TryFutureExt,
    },
    log::{error, info, warn},
    pin_utils::pin_mut,
    rand::Rng,
    std::{convert::Infallible, sync::Arc},
    wlan_common::hasher::WlanHasher,
    wlancfg_lib::{
        access_point::AccessPoint,
        client::{self, network_selection::NetworkSelector, scan},
        config_management::{SavedNetworksManager, SavedNetworksManagerApi},
        legacy::{self, IfaceRef},
        mode_management::{
            create_iface_manager, device_monitor,
            iface_manager_api::IfaceManagerApi,
            low_power_manager::PowerModeManager,
            phy_manager::{PhyManager, PhyManagerApi},
        },
        regulatory_manager::RegulatoryManager,
        telemetry::{
            connect_to_metrics_logger_factory, create_metrics_logger, serve_telemetry,
            TelemetrySender,
        },
        util,
    },
};

const REGULATORY_LISTENER_TIMEOUT_SEC: i64 = 30;

// Service name to persist Inspect data across boots
const PERSISTENCE_SERVICE_PATH: &str = "/svc/fuchsia.diagnostics.persist.DataPersistence-wlan";

async fn serve_fidl(
    ap: AccessPoint,
    configurator: legacy::deprecated_configuration::DeprecatedConfigurator,
    iface_manager: Arc<Mutex<dyn IfaceManagerApi + Send>>,
    legacy_client_ref: IfaceRef,
    saved_networks: Arc<dyn SavedNetworksManagerApi>,
    scan_requester: Arc<dyn scan::ScanRequestApi>,
    client_sender: util::listener::ClientListenerMessageSender,
    client_listener_msgs: mpsc::UnboundedReceiver<util::listener::ClientListenerMessage>,
    ap_listener_msgs: mpsc::UnboundedReceiver<util::listener::ApMessage>,
    regulatory_receiver: oneshot::Receiver<()>,
    telemetry_sender: TelemetrySender,
) -> Result<Infallible, Error> {
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
                scan_requester.clone(),
                client_provider_lock.clone(),
                reqs,
                telemetry_sender.clone(),
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
        fasync::Timer::new(1.minutes().after_now()).await;
    }
}

// wlancfg expects to be able to get updates from the RegulatoryRegionWatcher UNLESS the
// service is not present in wlancfg's sandbox OR the product configuration does not offer the
// service to wlancfg.  If the RegulatoryRegionWatcher is not available for either of these
// allowed reasons, wlancfg will continue serving the WLAN policy API in WW mode.
async fn run_regulatory_manager(
    iface_manager: Arc<Mutex<dyn IfaceManagerApi + Send>>,
    regulatory_sender: oneshot::Sender<()>,
) -> Result<(), Error> {
    // This initial connection will always succeed due to the presence of the protocol in the
    // component manifest.
    let req = match fuchsia_component::client::new_protocol_connector::<RegulatoryRegionWatcherMarker>(
    ) {
        Ok(req) => req,
        Err(e) => {
            warn!("error probing RegulatoryRegionWatcher service: {:?}", e);
            return Ok(());
        }
    };

    // An error here indicates that there is a missing `use` statement and the
    // RegulatoryRegionWatcher is not available in this namespace.  This should never happen since
    // the build system checks the capability routes and wlancfg needs this service.
    if !req.exists().await.context("error checking for RegulatoryRegionWatcher existence")? {
        warn!("RegulatoryRegionWatcher is not available per the component manifest");
        return Ok(());
    }

    // The connect call will always succeed because all it does is send the server end of a channel
    // to the directory that holds the capability.
    let regulatory_svc =
        req.connect().context("unable to connect RegulatoryRegionWatcher proxy")?;

    let regulatory_manager = RegulatoryManager::new(regulatory_svc, iface_manager);

    // The only way to test for the presence of the RegulatoryRegionWatcher service is to actually
    // use the handle to poll for updates.  If the RegulatoryManager future exits, simply log the
    // reason and return Ok(()) so that the WLAN policy API will continue to be served.
    if let Some(e) = regulatory_manager.run(regulatory_sender).await.err() {
        warn!("RegulatoryManager exiting: {:?}", e);
    }
    Ok(())
}

// wlancfg can respond to low power services updates provided the service is available.  If the
// service is not available, wlancfg will simply disable power save.  If the service is available,
// wlancfg will listen for updates to WLAN power level and apply the desired power configuration
// to all PHYs.
async fn run_low_power_manager(
    phy_manager: Arc<Mutex<PhyManager>>,
    telemetry_sender: TelemetrySender,
) -> Result<(), Error> {
    // Check if the low power service is offered to wlancfg.
    let req = match fuchsia_component::client::new_protocol_connector::<fidl_lp::ConnectorMarker>()
    {
        Ok(req) => req,
        Err(e) => {
            warn!("error probing low power client connector service: {:?}", e);
            return Ok(());
        }
    };

    // Only proceed with monitoring for updates if the Connector service exists and if we can
    // connect to it.
    if !req.exists().await.context("error checking for low power Connector existence")? {
        warn!("Low power Connector is not available");
        return Ok(());
    }

    // To ensure that the policy layer starts off in a known power state, set the PHYs to
    // performance mode.
    let mut phy_manager_lock = phy_manager.lock().await;
    if let Err(e) =
        phy_manager_lock.set_power_state(fidl_common::PowerSaveType::PsModePerformance).await
    {
        warn!("Failed to initialize PHYs to performance mode: {:?}", e);
    }
    drop(phy_manager_lock);

    // At this point, the low power service is known to exist and wlancfg will attempt to monitor
    // for low power updates and to apply the low power settings to all PHYs as new updates and
    // new PHYs are discovered.  Any error in this process should be considered fatal.
    let lp_connector = req.connect().context("Unable to connect to low power Connector service")?;
    let (watcher_proxy, watcher_service) =
        fidl::endpoints::create_proxy::<fidl_lp::WatcherMarker>()?;
    if let Err(e) = lp_connector.connect(fidl_lp::ClientType::Wlan, watcher_service) {
        warn!("Client level connector is unavailable: {:?}", e);
        return Ok(());
    }

    let lp_manager = PowerModeManager::new(watcher_proxy, phy_manager, telemetry_sender);
    lp_manager.run().await;
    Ok(())
}

async fn run_all_futures() -> Result<(), Error> {
    let monitor_svc = fuchsia_component::client::connect_to_protocol::<DeviceMonitorMarker>()
        .context("failed to connect to device monitor")?;
    let persistence_proxy = fuchsia_component::client::connect_to_protocol_at_path::<
        fidl_fuchsia_diagnostics_persist::DataPersistenceMarker,
    >(PERSISTENCE_SERVICE_PATH);
    let (persistence_req_sender, persistence_req_forwarder_fut) = match persistence_proxy {
        Ok(persistence_proxy) => {
            let (s, f) = auto_persist::create_persistence_req_sender(persistence_proxy);
            (s, OptionFuture::from(Some(f)))
        }
        Err(e) => {
            error!("Failed to connect to persistence service: {}", e);
            // To simplify the code, we still create mpsc::Sender, but there's nothing to forward
            // the tag to the Persistence service because we can't connect to it.
            // Note: because we drop the receiver here, be careful about log spam when sending
            //       tags through the `sender` below. This is automatically handled by
            //       `auto_persist::AutoPersist` because it only logs the first time sending
            //       fail, so just use that wrapper type instead of logging directly.
            let (sender, _receiver) = mpsc::channel::<String>(1);
            (sender, OptionFuture::from(None))
        }
    };

    // Cobalt 1.1
    let cobalt_1dot1_svc = connect_to_metrics_logger_factory().await?;
    let cobalt_1dot1_proxy = match create_metrics_logger(cobalt_1dot1_svc, None).await {
        Ok(proxy) => proxy,
        Err(e) => {
            warn!("Metrics logging is unavailable: {}", e);

            // If it is not possible to acquire a metrics logging proxy, create a disconnected
            // proxy and attempt to serve the policy API with metrics disabled.
            let (proxy, _) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
                    .context("failed to create MetricEventLoggerMarker endponts")?;
            proxy
        }
    };

    // According to doc, ThreadRng is cryptographically secure:
    // https://docs.rs/rand/0.5.0/rand/rngs/struct.ThreadRng.html
    //
    // The hash key is different from other components, making us not able to correlate
    // the same SSID and BSSID logged by each WLAN component.
    // TODO(fxbug.dev/70385): Share the hash key across wlanstack and wlancfg. This TODO
    //                        can also be closed once PII redaction for Inspect is
    //                        supported. (see fxbug.dev/fxbug.dev/71903)
    let hasher = WlanHasher::new(rand::thread_rng().gen::<u64>().to_le_bytes());
    let external_inspect_node = component::inspector().root().create_child("external");
    let (telemetry_sender, telemetry_fut) = serve_telemetry(
        monitor_svc.clone(),
        cobalt_1dot1_proxy,
        hasher.clone(),
        component::inspector().root().create_child("client_stats"),
        external_inspect_node.create_child("client_stats"),
        persistence_req_sender.clone(),
    );
    component::inspector().root().record(external_inspect_node);

    let (scan_request_sender, scan_request_receiver) =
        mpsc::channel::<scan::ScanRequest>(scan::SCAN_REQUEST_BUFFER_SIZE);
    let scan_requester = Arc::new(scan::ScanRequester { sender: scan_request_sender });
    let saved_networks = Arc::new(SavedNetworksManager::new(telemetry_sender.clone()).await?);
    let network_selector = Arc::new(NetworkSelector::new(
        saved_networks.clone(),
        scan_requester.clone(),
        hasher,
        component::inspector().root().create_child("network_selector"),
        persistence_req_sender.clone(),
        telemetry_sender.clone(),
    ));

    let phy_manager = Arc::new(Mutex::new(PhyManager::new(
        monitor_svc.clone(),
        component::inspector().root().create_child("phy_manager"),
        telemetry_sender.clone(),
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
        monitor_svc.clone(),
        saved_networks.clone(),
        network_selector.clone(),
        telemetry_sender.clone(),
    );

    let scanning_service = scan::serve_scanning_loop(
        iface_manager.clone(),
        saved_networks.clone(),
        telemetry_sender.clone(),
        scan_request_receiver,
    );

    let legacy_client = IfaceRef::new();
    let device_event_listener = device_monitor::Listener::new(
        monitor_svc.clone(),
        legacy_client.clone(),
        phy_manager.clone(),
        iface_manager.clone(),
    );

    let (regulatory_sender, regulatory_receiver) = oneshot::channel();
    let ap = AccessPoint::new(iface_manager.clone(), ap_sender, Arc::new(Mutex::new(())));
    let fidl_fut = serve_fidl(
        ap,
        configurator,
        iface_manager.clone(),
        legacy_client,
        saved_networks.clone(),
        scan_requester,
        client_sender,
        client_receiver,
        ap_receiver,
        regulatory_receiver,
        telemetry_sender.clone(),
    );

    let dev_watcher_fut = watcher_proxy
        .take_event_stream()
        .try_for_each(|evt| device_monitor::handle_event(&device_event_listener, evt).map(Ok))
        .err_into()
        .and_then(|_| {
            let result: Result<(), Error> =
                Err(format_err!("Device watcher future exited unexpectedly"));
            future::ready(result)
        });

    let saved_networks_metrics_fut = saved_networks_manager_metrics_loop(saved_networks.clone());
    let regulatory_fut = run_regulatory_manager(iface_manager.clone(), regulatory_sender);
    let low_power_fut = run_low_power_manager(phy_manager.clone(), telemetry_sender);

    let _ = futures::try_join!(
        fidl_fut,
        dev_watcher_fut,
        iface_manager_service,
        saved_networks_metrics_fut.map(Ok),
        scanning_service,
        regulatory_fut,
        low_power_fut,
        telemetry_fut.map(Ok),
        persistence_req_forwarder_fut.map(Ok),
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
