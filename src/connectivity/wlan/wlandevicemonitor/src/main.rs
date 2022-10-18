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
        channel::mpsc,
        future::{try_join4, BoxFuture},
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
    new_iface_sink: mpsc::UnboundedSender<device::NewIface>,
) -> Result<(), Error> {
    fs.dir("svc").add_fidl_service(move |reqs| {
        let fut = service::serve_monitor_requests(
            reqs,
            phys.clone(),
            ifaces.clone(),
            watcher_service.clone(),
            dev_svc.clone(),
            new_iface_sink.clone(),
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
) -> BoxFuture<'static, Result<std::convert::Infallible, Error>> {
    info!("Serving IsolatedDevMgr environment");
    let fut = device::serve_phys::<isolated_devmgr::IsolatedDeviceEnv>(phys, inspect_tree);
    Box::pin(fut)
}

#[cfg(not(feature = "isolated_dev_mgr"))]
fn serve_phys(
    phys: Arc<device::PhyMap>,
    inspect_tree: Arc<inspect::WlanMonitorTree>,
) -> BoxFuture<'static, Result<std::convert::Infallible, Error>> {
    info!("Serving real device environment");
    let fut = device::serve_phys::<wlan_dev::RealDeviceEnv>(phys, inspect_tree);
    Box::pin(fut)
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    // Initialize logging with a tag that can be used to select these logs for forwarding to console
    syslog::init_with_tags(&["wlan"]).expect("Syslog init should not fail");
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

    let (new_iface_sink, new_iface_stream) = mpsc::unbounded();
    let fidl_fut =
        serve_fidl(fs, phys.clone(), ifaces.clone(), watcher_service, dev_svc, new_iface_sink);

    let new_iface_fut =
        service::handle_new_iface_stream(phys.clone(), ifaces.clone(), new_iface_stream);

    let ((), (), (), ()) = try_join4(
        fidl_fut,
        phy_server.map_ok(|_: std::convert::Infallible| ()),
        watcher_fut.map_ok(|_: std::convert::Infallible| ()),
        new_iface_fut,
    )
    .await?;
    error!("Exiting");
    Ok(())
}
