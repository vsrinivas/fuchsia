// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! System service for wireless networking

#![deny(missing_docs)]
#![recursion_limit = "256"]

mod device;
mod future_util;
mod inspect;
mod mlme_query_proxy;
mod service;
mod station;
mod stats_scheduler;
mod telemetry;
#[cfg(test)]
pub mod test_helper;

use anyhow::{Context, Error};
use argh::FromArgs;
use fidl_fuchsia_wlan_device_service::DeviceServiceRequestStream;
use fuchsia_async as fasync;
use fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType};
use fuchsia_component::server::{ServiceFs, ServiceObjLocal};
use fuchsia_inspect::Inspector;
use fuchsia_inspect_contrib::auto_persist;
use fuchsia_syslog as syslog;
use futures::future::try_join4;
use futures::prelude::*;
use log::{error, info};
use std::sync::Arc;
use wlan_sme;

use crate::device::IfaceMap;

const CONCURRENT_LIMIT: usize = 1000;

// Service name to persist Inspect data across boots
const PERSISTENCE_SERVICE_PATH: &str = "/svc/fuchsia.diagnostics.persist.DataPersistence-wlan";

/// Configuration for wlanstack service.
/// This configuration is a super set of individual component configurations such as SME.
#[derive(FromArgs, Clone, Debug, Default)]
pub struct ServiceCfg {
    /// if WEP should be supported by the service instance.
    #[argh(switch)]
    pub wep_supported: bool,
    /// if legacy WPA1 should be supported by the service instance.
    #[argh(switch)]
    pub wpa1_supported: bool,
}

impl From<ServiceCfg> for wlan_sme::Config {
    fn from(cfg: ServiceCfg) -> Self {
        Self { wep_supported: cfg.wep_supported, wpa1_supported: cfg.wpa1_supported }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Initialize logging with a tag that can be used to select these logs for forwarding to console
    syslog::init_with_tags(&["wlan"]).expect("Syslog init should not fail");

    info!("Starting");
    let cfg: ServiceCfg = argh::from_env();
    info!("{:?}", cfg);

    let mut fs = ServiceFs::new_local();
    let inspector = Inspector::new_with_size(inspect::VMO_SIZE_BYTES);
    inspect_runtime::serve(&inspector, &mut fs)?;

    let persistence_proxy = fuchsia_component::client::connect_to_protocol_at_path::<
        fidl_fuchsia_diagnostics_persist::DataPersistenceMarker,
    >(PERSISTENCE_SERVICE_PATH)
    .context("failed to connect to persistence service")?;
    let (persistence_req_sender, persistence_req_forwarder_fut) =
        auto_persist::create_persistence_req_sender(persistence_proxy);

    let inspect_tree = Arc::new(inspect::WlanstackTree::new(inspector, persistence_req_sender));
    fs.dir("svc").add_fidl_service(IncomingServices::Device);

    let ifaces = IfaceMap::new();
    let ifaces = Arc::new(ifaces);

    let cobalt_1dot1_svc = fuchsia_component::client::connect_to_protocol::<
        fidl_fuchsia_metrics::MetricEventLoggerFactoryMarker,
    >()
    .context("failed to connect to metrics service")?;
    let (cobalt_1dot1_proxy, cobalt_1dot1_server) =
        fidl::endpoints::create_proxy::<fidl_fuchsia_metrics::MetricEventLoggerMarker>()
            .context("failed to create MetricEventLoggerMarker endponts")?;
    let project_spec = fidl_fuchsia_metrics::ProjectSpec {
        customer_id: None, // defaults to fuchsia
        project_id: Some(wlan_metrics_registry::PROJECT_ID),
        ..fidl_fuchsia_metrics::ProjectSpec::EMPTY
    };
    if let Err(e) =
        cobalt_1dot1_svc.create_metric_event_logger(project_spec, cobalt_1dot1_server).await
    {
        error!("create_metric_event_logger failure: {}", e);
    }

    let (cobalt_sender, cobalt_reporter) = CobaltConnector::default()
        .serve(ConnectionType::project_id(wlan_metrics_registry::PROJECT_ID));
    let telemetry_server = telemetry::report_telemetry_periodically(
        ifaces.clone(),
        cobalt_sender.clone(),
        inspect_tree.clone(),
    );

    let dev_monitor = fuchsia_component::client::connect_to_protocol::<
        fidl_fuchsia_wlan_device_service::DeviceMonitorMarker,
    >()
    .context("Failed to connect to DeviceMonitor.")?;
    let serve_fidl_fut =
        serve_fidl(cfg, fs, ifaces, inspect_tree, cobalt_sender, cobalt_1dot1_proxy, dev_monitor);

    let ((), (), (), ()) = try_join4(
        serve_fidl_fut,
        cobalt_reporter.map(Ok),
        persistence_req_forwarder_fut.map(Ok),
        telemetry_server.map(Ok),
    )
    .await?;
    info!("Exiting");
    Ok(())
}

enum IncomingServices {
    Device(DeviceServiceRequestStream),
}

async fn serve_fidl(
    cfg: ServiceCfg,
    mut fs: ServiceFs<ServiceObjLocal<'_, IncomingServices>>,
    ifaces: Arc<IfaceMap>,
    inspect_tree: Arc<inspect::WlanstackTree>,
    cobalt_sender: CobaltSender,
    cobalt_1dot1_proxy: fidl_fuchsia_metrics::MetricEventLoggerProxy,
    dev_monitor_proxy: fidl_fuchsia_wlan_device_service::DeviceMonitorProxy,
) -> Result<(), Error> {
    fs.take_and_serve_directory_handle()?;
    let iface_counter = Arc::new(service::IfaceCounter::new());

    let fdio_server = fs.for_each_concurrent(CONCURRENT_LIMIT, move |s| {
        let ifaces = ifaces.clone();
        let cobalt_sender = cobalt_sender.clone();
        let cobalt_1dot1_proxy = cobalt_1dot1_proxy.clone();
        let cfg = cfg.clone();
        let inspect_tree = inspect_tree.clone();
        let iface_counter = iface_counter.clone();
        let dev_monitor_proxy = dev_monitor_proxy.clone();
        async move {
            match s {
                IncomingServices::Device(stream) => {
                    service::serve_device_requests(
                        iface_counter,
                        cfg,
                        ifaces,
                        stream,
                        inspect_tree,
                        cobalt_sender,
                        cobalt_1dot1_proxy,
                        dev_monitor_proxy,
                    )
                    .unwrap_or_else(|e| println!("{:?}", e))
                    .await
                }
            }
        }
    });
    fdio_server.await;
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn parse_svc_cfg_wep() {
        let cfg = ServiceCfg::from_args(&["bin/app"], &["--wep-supported"]).unwrap();
        assert!(cfg.wep_supported);
    }

    #[test]
    fn parse_svc_cfg_default() {
        let cfg = ServiceCfg::from_args(&["bin/app"], &[]).unwrap();
        assert!(!cfg.wep_supported);
    }

    #[test]
    fn svc_to_sme_cfg() {
        let svc_cfg = ServiceCfg::from_args(&["bin/app"], &[]).unwrap();
        let sme_cfg: wlan_sme::Config = svc_cfg.into();
        assert!(!sme_cfg.wep_supported);

        let svc_cfg = ServiceCfg::from_args(&["bin/app"], &["--wep-supported"]).unwrap();
        let sme_cfg: wlan_sme::Config = svc_cfg.into();
        assert!(sme_cfg.wep_supported);
    }
}
