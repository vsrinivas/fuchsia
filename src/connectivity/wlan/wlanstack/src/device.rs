// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{bail, format_err, Error},
    fidl_fuchsia_wlan_device as fidl_wlan_dev,
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, DeviceInfo},
    fuchsia_cobalt::CobaltSender,
    fuchsia_wlan_dev as wlan_dev,
    futures::{
        channel::mpsc,
        future::{Future, FutureExt, FutureObj},
        select,
        stream::{FuturesUnordered, Stream, StreamExt, TryStreamExt},
    },
    log::{error, info},
    std::{marker::Unpin, sync::Arc},
    void::Void,
    wlan_inspect,
    wlan_sme::{self, clone_utils},
};

use crate::{
    device_watch::{self, NewIfaceDevice},
    inspect,
    mlme_query_proxy::MlmeQueryProxy,
    station,
    stats_scheduler::{self, StatsScheduler},
    watchable_map::WatchableMap,
    ServiceCfg,
};

// TODO(WLAN-927): Obsolete once all drivers support this feature.
#[derive(Debug)]
pub enum DirectMlmeChannel {
    NotSupported,
    Supported(fidl_mlme::MlmeProxy),
}

#[derive(Debug)]
pub struct NewIface {
    // Global iface ID.
    pub id: u16,
    // Iface's global PHY ID.
    pub phy_id: u16,
    // Local ID assigned by this iface's PHY.
    pub phy_assigned_id: u16,
    // A channel to communicate with the iface's underlying MLME.
    // The MLME proxy is only available if the device driver indicates support in its
    // feature flags. See WLAN-927 (direct SME Channel support).
    pub mlme_channel: DirectMlmeChannel,
}

pub struct PhyDevice {
    pub proxy: fidl_wlan_dev::PhyProxy,
    pub device: wlan_dev::Device,
}

pub type ClientSmeServer = mpsc::UnboundedSender<super::station::client::Endpoint>;
pub type ApSmeServer = mpsc::UnboundedSender<super::station::ap::Endpoint>;
pub type MeshSmeServer = mpsc::UnboundedSender<super::station::mesh::Endpoint>;

pub enum SmeServer {
    Client(ClientSmeServer),
    Ap(ApSmeServer),
    Mesh(MeshSmeServer),
}

pub struct IfaceDevice {
    pub sme_server: SmeServer,
    pub stats_sched: StatsScheduler,
    // TODO(WLAN-927): Remove. Present for drivers which don't support new SME channel.
    pub device: Option<wlan_dev::Device>,
    pub mlme_query: MlmeQueryProxy,
    pub device_info: DeviceInfo,
}

pub type PhyMap = WatchableMap<u16, PhyDevice>;
pub type IfaceMap = WatchableMap<u16, IfaceDevice>;

pub async fn serve_phys(phys: Arc<PhyMap>) -> Result<Void, Error> {
    let mut new_phys = device_watch::watch_phy_devices()?;
    let mut active_phys = FuturesUnordered::new();
    loop {
        select! {
            // OK to fuse directly in the `select!` since we bail immediately
            // when a `None` is encountered.
            new_phy = new_phys.next().fuse() => match new_phy {
                None => bail!("new phy stream unexpectedly finished"),
                Some(Err(e)) => bail!("new phy stream returned an error: {}", e),
                Some(Ok(new_phy)) => {
                    let fut = serve_phy(&phys, new_phy);
                    active_phys.push(fut);
                }
            },
            () = active_phys.select_next_some() => {},
        }
    }
}

async fn serve_phy(phys: &PhyMap, new_phy: device_watch::NewPhyDevice) {
    info!("new phy #{}: {}", new_phy.id, new_phy.device.path().to_string_lossy());
    let id = new_phy.id;
    let event_stream = new_phy.proxy.take_event_stream();
    phys.insert(id, PhyDevice { proxy: new_phy.proxy, device: new_phy.device });
    let r = await!(event_stream.map_ok(|_| ()).try_collect::<()>());
    phys.remove(&id);
    if let Err(e) = r {
        error!("error reading from the FIDL channel of phy #{}: {}", id, e);
    }
    info!("phy removed: #{}", id);
}

pub async fn serve_ifaces(
    cfg: ServiceCfg,
    ifaces: Arc<IfaceMap>,
    cobalt_sender: CobaltSender,
    inspect_tree: Arc<inspect::WlanstackTree>,
) -> Result<Void, Error> {
    #[allow(deprecated)]
    let mut new_ifaces = device_watch::watch_iface_devices()?;
    let mut active_ifaces = FuturesUnordered::new();
    loop {
        select! {
            new_iface = new_ifaces.next().fuse() => match new_iface {
                None => bail!("new iface stream unexpectedly finished"),
                Some(Err(e)) => bail!("new iface stream returned an error: {}", e),
                Some(Ok(new_iface)) => {
                    let iface_id = new_iface.id;

                    let inspect_tree = inspect_tree.clone();
                    let iface_tree_holder = inspect_tree.create_iface_child(iface_id);
                    #[allow(deprecated)]
                    let fut = query_and_serve_iface_deprecated(
                    cfg.clone(),
                            new_iface, &ifaces, cobalt_sender.clone(), iface_tree_holder
                        ).then(move |_| async move {
                            inspect_tree.notify_iface_removed(iface_id);
                        });
                    active_ifaces.push(fut);
                }
            },
            () = active_ifaces.select_next_some() => {},
        }
    }
}

#[deprecated(note = "function is obsolete once WLAN-927 landed")]
async fn query_and_serve_iface_deprecated(
    cfg: ServiceCfg,
    new_iface: NewIfaceDevice,
    ifaces: &IfaceMap,
    cobalt_sender: CobaltSender,
    iface_tree_holder: Arc<wlan_inspect::iface_mgr::IfaceTreeHolder>,
) {
    let NewIfaceDevice { id, device, proxy } = new_iface;
    let event_stream = proxy.take_event_stream();
    let (stats_sched, stats_reqs) = stats_scheduler::create_scheduler();

    let device_info = match await!(proxy.query_device_info()) {
        Ok(x) => x,
        Err(e) => {
            error!("Failed to query new iface '{}': {}", device.path().display(), e);
            return;
        }
    };
    let result = create_sme(
        cfg,
        proxy.clone(),
        event_stream,
        &device_info,
        stats_reqs,
        cobalt_sender,
        iface_tree_holder,
    );
    let (sme, sme_fut) = match result {
        Ok(x) => x,
        Err(e) => {
            error!("Failed to create SME for new iface '{}': {}", device.path().display(), e);
            return;
        }
    };

    info!(
        "new iface #{} with role '{:?}': {}",
        id,
        device_info.role,
        device.path().to_string_lossy()
    );
    let mlme_query = MlmeQueryProxy::new(proxy);
    ifaces.insert(
        id,
        IfaceDevice { sme_server: sme, stats_sched, device: Some(device), mlme_query, device_info },
    );

    let r = await!(sme_fut);
    if let Err(e) = r {
        error!("Error serving station for iface #{}: {}", id, e);
    }
    ifaces.remove(&id);
    info!("iface removed: {}", id);
}

pub async fn query_and_serve_iface(
    cfg: ServiceCfg,
    iface_id: u16,
    mlme_proxy: fidl_mlme::MlmeProxy,
    ifaces: Arc<IfaceMap>,
    iface_tree_holder: Arc<wlan_inspect::iface_mgr::IfaceTreeHolder>,
    cobalt_sender: CobaltSender,
) -> Result<(), failure::Error> {
    let event_stream = mlme_proxy.take_event_stream();
    let (stats_sched, stats_reqs) = stats_scheduler::create_scheduler();

    let device_info = await!(mlme_proxy.query_device_info())
        .map_err(|e| format_err!("failed querying iface: {}", e))?;
    let (sme, sme_fut) = create_sme(
        cfg,
        mlme_proxy.clone(),
        event_stream,
        &device_info,
        stats_reqs,
        cobalt_sender,
        iface_tree_holder,
    )
    .map_err(|e| format_err!("failed to creating SME: {}", e))?;

    info!("new iface #{} with role '{:?}'", iface_id, device_info.role,);
    let mlme_query = MlmeQueryProxy::new(mlme_proxy);
    ifaces.insert(
        iface_id,
        IfaceDevice { sme_server: sme, stats_sched, device: None, mlme_query, device_info },
    );

    let result = await!(sme_fut).map_err(|e| format_err!("error while serving SME: {}", e));
    info!("iface removed: {}", iface_id);
    ifaces.remove(&iface_id);
    result
}

fn create_sme<S>(
    cfg: ServiceCfg,
    proxy: fidl_mlme::MlmeProxy,
    event_stream: fidl_mlme::MlmeEventStream,
    device_info: &DeviceInfo,
    stats_requests: S,
    cobalt_sender: CobaltSender,
    iface_tree_holder: Arc<wlan_inspect::iface_mgr::IfaceTreeHolder>,
) -> Result<(SmeServer, impl Future<Output = Result<(), Error>>), Error>
where
    S: Stream<Item = stats_scheduler::StatsRequest> + Send + Unpin + 'static,
{
    let role = device_info.role;
    let device_info = wlan_sme::DeviceInfo {
        addr: device_info.mac_addr,
        bands: clone_utils::clone_bands(&device_info.bands),
        driver_features: device_info.driver_features.clone(),
    };

    match role {
        fidl_mlme::MacRole::Client => {
            let (sender, receiver) = mpsc::unbounded();
            let fut = station::client::serve(
                cfg.into(),
                proxy,
                device_info,
                event_stream,
                receiver,
                stats_requests,
                cobalt_sender,
                iface_tree_holder,
            );
            Ok((SmeServer::Client(sender), FutureObj::new(Box::new(fut))))
        }
        fidl_mlme::MacRole::Ap => {
            let (sender, receiver) = mpsc::unbounded();
            let fut =
                station::ap::serve(proxy, device_info, event_stream, receiver, stats_requests);
            Ok((SmeServer::Ap(sender), FutureObj::new(Box::new(fut))))
        }
        fidl_mlme::MacRole::Mesh => {
            let (sender, receiver) = mpsc::unbounded();
            let fut =
                station::mesh::serve(proxy, device_info, event_stream, receiver, stats_requests);
            Ok((SmeServer::Mesh(sender), FutureObj::new(Box::new(fut))))
        }
    }
}
