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
    argh::FromArgs,
    fuchsia_async as fasync,
    fuchsia_component::server::{ServiceFs, ServiceObjLocal},
    fuchsia_inspect::Inspector,
    futures::{future::try_join3, FutureExt, StreamExt, TryFutureExt},
    log::{error, info},
    std::sync::Arc,
};

/// Configuration for wlan-monitor service.
#[derive(FromArgs, Clone, Debug, Default)]
pub struct ServiceCfg {
    /// if devices are spawned in an isolated devmgr and device_watcher should watch devices
    /// in the isolated devmgr (for wlan-hw-sim based tests)
    #[argh(switch)]
    pub isolated_devmgr: bool,
}

async fn serve_fidl(
    mut fs: ServiceFs<ServiceObjLocal<'_, ()>>,
    phys: Arc<device::PhyMap>,
    watcher_service: watcher_service::WatcherService<device::PhyDevice, device::IfaceDevice>,
) -> Result<(), Error> {
    fs.dir("svc").add_fidl_service(move |reqs| {
        let fut = service::serve_monitor_requests(reqs, phys.clone(), watcher_service.clone())
            .unwrap_or_else(|e| error!("error serving device monitor API: {}", e));
        fasync::Task::spawn(fut).detach()
    });
    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    info!("Starting");
    let cfg: ServiceCfg = argh::from_env();
    info!("{:?}", cfg);

    let (phys, phy_events) = device::PhyMap::new();
    let phys = Arc::new(phys);
    let (ifaces, iface_events) = device::IfaceMap::new();
    let ifaces = Arc::new(ifaces);

    let (watcher_service, watcher_fut) =
        watcher_service::serve_watchers(phys.clone(), ifaces.clone(), phy_events, iface_events);

    let mut fs = ServiceFs::new_local();

    let inspector = Inspector::new_with_size(inspect::VMO_SIZE_BYTES);
    inspect_runtime::serve(&inspector, &mut fs)?;
    let inspect_tree = Arc::new(inspect::WlanMonitorTree::new(inspector));

    // TODO(fxbug.dev/45790): this should not depend on isolated_devmgr; the two functionalities should
    // live in separate binaries to avoid leaking test dependencies into production builds.
    let phy_server = if cfg.isolated_devmgr {
        device::serve_phys::<isolated_devmgr::IsolatedDeviceEnv>(phys.clone(), inspect_tree.clone())
            .left_future()
    } else {
        device::serve_phys::<wlan_dev::RealDeviceEnv>(phys.clone(), inspect_tree.clone())
            .right_future()
    };

    let fidl_fut = serve_fidl(fs, phys, watcher_service);

    let ((), (), ()) = try_join3(
        fidl_fut,
        phy_server.map_ok(|_: void::Void| ()),
        watcher_fut.map_ok(|_: void::Void| ()),
    )
    .await?;
    error!("Exiting");
    Ok(())
}
