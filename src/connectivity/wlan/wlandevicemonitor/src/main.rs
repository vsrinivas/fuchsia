// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod device;
mod device_watch;
mod inspect;
mod service;
mod watchable_map;
mod watcher_service;

use {
    anyhow::Error,
    fidl_fuchsia_wlan_device_service as fidl_svc, fuchsia_async as fasync,
    fuchsia_component::{
        client::connect_to_protocol,
        server::{ServiceFs, ServiceObjLocal},
    },
    fuchsia_inspect::Inspector,
    fuchsia_syslog as syslog,
    futures::{
        future::{try_join3, BoxFuture},
        StreamExt, TryFutureExt,
    },
    log::{error, info},
    std::sync::Arc,
};

const MAX_LOG_LEVEL: log::LevelFilter = log::LevelFilter::Info;

async fn serve_fidl(
    mut fs: ServiceFs<ServiceObjLocal<'_, ()>>,
    phys: Arc<device::PhyMap>,
    ifaces: Arc<device::IfaceMap>,
    watcher_service: watcher_service::WatcherService<device::PhyDevice, device::IfaceDevice>,
    dev_svc: fidl_svc::DeviceServiceProxy,
) -> Result<(), Error> {
    fs.dir("svc").add_fidl_service(move |reqs| {
        let fut = service::serve_monitor_requests(
            reqs,
            phys.clone(),
            ifaces.clone(),
            watcher_service.clone(),
            dev_svc.clone(),
        )
        .unwrap_or_else(|e| error!("error serving device monitor API: {}", e));
        fasync::Task::spawn(fut).detach()
    });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}

// Depending on the build configuration, compile in support for either the isolated device manager
// or the real device environment.  This eliminates the need for having test code in the production
// build of wlandevicemonitor.
#[cfg(feature = "isolated_dev_mgr")]
fn serve_phys(
    phys: Arc<device::PhyMap>,
    inspect_tree: Arc<inspect::WlanMonitorTree>,
) -> BoxFuture<'static, Result<void::Void, Error>> {
    info!("Serving IsolatedDevMgr environment");
    let fut = device::serve_phys::<isolated_devmgr::IsolatedDeviceEnv>(phys, inspect_tree);
    Box::pin(fut)
}

#[cfg(not(feature = "isolated_dev_mgr"))]
fn serve_phys(
    phys: Arc<device::PhyMap>,
    inspect_tree: Arc<inspect::WlanMonitorTree>,
) -> BoxFuture<'static, Result<void::Void, Error>> {
    info!("Serving real device environment");
    let fut = device::serve_phys::<wlan_dev::RealDeviceEnv>(phys, inspect_tree);
    Box::pin(fut)
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init().expect("Syslog init should not fail");
    log::set_max_level(MAX_LOG_LEVEL);

    info!("Starting");

    let (phys, phy_events) = device::PhyMap::new();
    let phys = Arc::new(phys);
    let (ifaces, iface_events) = device::IfaceMap::new();
    let ifaces = Arc::new(ifaces);

    let dev_svc = connect_to_protocol::<fidl_svc::DeviceServiceMarker>()?;

    let (watcher_service, watcher_fut) =
        watcher_service::serve_watchers(phys.clone(), ifaces.clone(), phy_events, iface_events);

    let mut fs = ServiceFs::new_local();

    let inspector = Inspector::new_with_size(inspect::VMO_SIZE_BYTES);
    inspect_runtime::serve(&inspector, &mut fs)?;
    let inspect_tree = Arc::new(inspect::WlanMonitorTree::new(inspector));

    let phy_server = serve_phys(phys.clone(), inspect_tree.clone());
    let fidl_fut = serve_fidl(fs, phys, ifaces, watcher_service, dev_svc);

    let ((), (), ()) = try_join3(
        fidl_fut,
        phy_server.map_ok(|_: void::Void| ()),
        watcher_fut.map_ok(|_: void::Void| ()),
    )
    .await?;
    error!("Exiting");
    Ok(())
}
