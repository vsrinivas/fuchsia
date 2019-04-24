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
    parking_lot::Mutex,
    std::{marker::Unpin, sync::Arc},
    void::Void,
    wlan_inspect,
    wlan_sme::{self, clone_utils},
};

use crate::{
    device_watch::{self, NewIfaceDevice},
    mlme_query_proxy::MlmeQueryProxy,
    station,
    stats_scheduler::{self, StatsScheduler},
    watchable_map::WatchableMap,
};

const MAX_DEAD_IFACE_NODES: usize = 3;

#[derive(Debug)]
pub struct NewIface {
    // Global iface ID.
    pub id: u16,
    // Iface's global PHY ID.
    pub phy_id: u16,
    // Local ID assigned by this iface's PHY.
    pub phy_assigned_id: u16,
    // TODO(WLAN-927): mlme_proxy is None if the iface's driver doesn't support SME channels.
    pub mlme_proxy: Option<fidl_mlme::MlmeProxy>,
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
    ifaces: Arc<IfaceMap>,
    cobalt_sender: CobaltSender,
    inspect_root: wlan_inspect::SharedNodePtr,
) -> Result<Void, Error> {
    #[allow(deprecated)]
    let mut new_ifaces = device_watch::watch_iface_devices()?;
    let mut active_ifaces = FuturesUnordered::new();
    let inspect_ifacemgr =
        Arc::new(Mutex::new(wlan_inspect::IfaceManager::new(inspect_root, MAX_DEAD_IFACE_NODES)));
    loop {
        select! {
            new_iface = new_ifaces.next().fuse() => match new_iface {
                None => bail!("new iface stream unexpectedly finished"),
                Some(Err(e)) => bail!("new iface stream returned an error: {}", e),
                Some(Ok(new_iface)) => {
                    let iface_id = new_iface.id;
                    let inspect_ifacemgr = inspect_ifacemgr.clone();
                    let inspect_sme = inspect_ifacemgr.lock().create_iface_child(iface_id);
                    #[allow(deprecated)]
                    let fut = query_and_serve_iface_deprecated(
                            new_iface, &ifaces, cobalt_sender.clone(), inspect_sme
                        ).then(move |_| async move {
                            inspect_ifacemgr.lock().notify_iface_removed(iface_id);
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
    new_iface: NewIfaceDevice,
    ifaces: &IfaceMap,
    cobalt_sender: CobaltSender,
    inspect_sme: wlan_inspect::SharedNodePtr,
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
        proxy.clone(),
        event_stream,
        &device_info,
        stats_reqs,
        cobalt_sender,
        inspect_sme,
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
    new_iface: NewIface,
    ifaces: Arc<IfaceMap>,
    inspect_sme: wlan_inspect::SharedNodePtr,
    cobalt_sender: CobaltSender,
) -> Result<(), failure::Error> {
    let mlme_proxy = new_iface.mlme_proxy.expect("MlmeProxy must not be None");
    let event_stream = mlme_proxy.take_event_stream();
    let (stats_sched, stats_reqs) = stats_scheduler::create_scheduler();

    let device_info = await!(mlme_proxy.query_device_info())
        .map_err(|e| format_err!("failed querying iface: {}", e))?;
    let (sme, sme_fut) = create_sme(
        mlme_proxy.clone(),
        event_stream,
        &device_info,
        stats_reqs,
        cobalt_sender,
        inspect_sme,
    )
    .map_err(|e| format_err!("failed to creating SME: {}", e))?;

    info!("new iface #{} with role '{:?}'", new_iface.id, device_info.role,);
    let mlme_query = MlmeQueryProxy::new(mlme_proxy);
    ifaces.insert(
        new_iface.id,
        IfaceDevice { sme_server: sme, stats_sched, device: None, mlme_query, device_info },
    );

    let result = await!(sme_fut).map_err(|e| format_err!("error while serving SME: {}", e));
    info!("iface removed: {}", new_iface.id);
    ifaces.remove(&new_iface.id);
    result
}

fn create_sme<S>(
    proxy: fidl_mlme::MlmeProxy,
    event_stream: fidl_mlme::MlmeEventStream,
    device_info: &DeviceInfo,
    stats_requests: S,
    cobalt_sender: CobaltSender,
    inspect_sme: wlan_inspect::SharedNodePtr,
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
                proxy,
                device_info,
                event_stream,
                receiver,
                stats_requests,
                cobalt_sender,
                inspect_sme,
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
