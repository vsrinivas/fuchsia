// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! System service for wireless networking

#![deny(missing_docs)]
#![recursion_limit = "256"]

mod device;
mod device_watch;
mod future_util;
mod inspect;
mod logger;
mod mlme_query_proxy;
mod service;
mod station;
mod stats_scheduler;
mod telemetry;
mod watchable_map;
mod watcher_service;

use anyhow::Error;
use argh::FromArgs;
use fidl_fuchsia_wlan_device_service::DeviceServiceRequestStream;
use fuchsia_async as fasync;
use fuchsia_cobalt::{CobaltConnector, CobaltSender, ConnectionType};
use fuchsia_component::server::{ServiceFs, ServiceObjLocal};
use fuchsia_inspect::Inspector;
use futures::future::try_join5;
use futures::prelude::*;
use log::info;
use std::sync::Arc;
use wlan_sme;

use crate::device::{IfaceDevice, IfaceMap, PhyDevice, PhyMap};
use crate::watcher_service::WatcherService;

const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;

const CONCURRENT_LIMIT: usize = 1000;

static LOGGER: logger::Logger = logger::Logger;

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
    /// if devices are spawned in an isolated devmgr and device_watcher should watch devices
    /// in the isolated devmgr (for wlan-hw-sim based tests)
    #[argh(switch)]
    pub isolated_devmgr: bool,
}

impl From<ServiceCfg> for wlan_sme::Config {
    fn from(cfg: ServiceCfg) -> Self {
        Self { wep_supported: cfg.wep_supported, wpa1_supported: cfg.wpa1_supported }
    }
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    log::set_logger(&LOGGER)?;
    log::set_max_level(MAX_LOG_LEVEL);

    info!("Starting");
    let cfg: ServiceCfg = argh::from_env();
    info!("{:?}", cfg);
    let mut fs = ServiceFs::new_local();
    let inspector = Inspector::new_with_size(inspect::VMO_SIZE_BYTES);
    inspector.serve(&mut fs)?;
    let inspect_tree = Arc::new(inspect::WlanstackTree::new(inspector));
    fs.dir("svc").add_fidl_service(IncomingServices::Device);

    let (phys, phy_events) = device::PhyMap::new();
    let (ifaces, iface_events) = device::IfaceMap::new();
    let phys = Arc::new(phys);
    let ifaces = Arc::new(ifaces);

    // TODO(45790): this should not depend on isolated_devmgr; the two functionalities should
    // live in separate binaries to avoid leaking test dependencies into production builds.
    let phy_server = if cfg.isolated_devmgr {
        device::serve_phys::<isolated_devmgr::IsolatedDeviceEnv>(phys.clone(), inspect_tree.clone())
            .left_future()
    } else {
        device::serve_phys::<wlan_dev::RealDeviceEnv>(phys.clone(), inspect_tree.clone())
            .right_future()
    };
    let (cobalt_sender, cobalt_reporter) = CobaltConnector::default()
        .serve(ConnectionType::project_id(wlan_metrics_registry::PROJECT_ID));
    let telemetry_server =
        telemetry::report_telemetry_periodically(ifaces.clone(), cobalt_sender.clone());
    let (watcher_service, watcher_fut) =
        watcher_service::serve_watchers(phys.clone(), ifaces.clone(), phy_events, iface_events);
    let serve_fidl_fut =
        serve_fidl(cfg, fs, phys, ifaces, watcher_service, inspect_tree, cobalt_sender);

    let ((), (), (), (), ()) = try_join5(
        serve_fidl_fut,
        watcher_fut.map_ok(|_: void::Void| ()),
        phy_server.map_ok(|_: void::Void| ()),
        cobalt_reporter.map(Ok),
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
    phys: Arc<PhyMap>,
    ifaces: Arc<IfaceMap>,
    watcher_service: WatcherService<PhyDevice, IfaceDevice>,
    inspect_tree: Arc<inspect::WlanstackTree>,
    cobalt_sender: CobaltSender,
) -> Result<(), Error> {
    fs.take_and_serve_directory_handle()?;
    let iface_counter = Arc::new(service::IfaceCounter::new());

    let fdio_server = fs.for_each_concurrent(CONCURRENT_LIMIT, move |s| {
        let phys = phys.clone();
        let ifaces = ifaces.clone();
        let watcher_service = watcher_service.clone();
        let cobalt_sender = cobalt_sender.clone();
        let cfg = cfg.clone();
        let inspect_tree = inspect_tree.clone();
        let iface_counter = iface_counter.clone();
        async move {
            match s {
                IncomingServices::Device(stream) => {
                    service::serve_device_requests(
                        iface_counter,
                        cfg,
                        phys,
                        ifaces,
                        watcher_service,
                        stream,
                        inspect_tree,
                        cobalt_sender,
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
    use {super::*, std::panic};

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
