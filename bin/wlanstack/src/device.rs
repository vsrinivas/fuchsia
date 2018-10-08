// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{bail, Error, format_err, ResultExt},
    fidl_fuchsia_wlan_device as fidl_wlan_dev,
    fidl_fuchsia_wlan_mlme::{self as fidl_mlme, DeviceQueryConfirm, MlmeEventStream},
    fuchsia_async::Timer,
    fuchsia_wlan_dev as wlan_dev,
    fuchsia_zircon::prelude::*,
    futures::{
        channel::mpsc,
        future::{Future, FutureObj},
        select,
        stream::{Stream, StreamExt, TryStreamExt},
    },
    log::{error, info, warn},
    pin_utils::pin_mut,
    std::{
        collections::HashSet,
        marker::Unpin,
        sync::Arc,
    },
    wlan_sme,
};

use crate::{
    cobalt_reporter::CobaltSender,
    device_watch::{self, NewIfaceDevice},
    future_util::ConcurrentTasks,
    mlme_query_proxy::MlmeQueryProxy,
    Never,
    station,
    stats_scheduler::{self, StatsScheduler},
    watchable_map::WatchableMap,
};

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
    pub device: wlan_dev::Device,
    pub mlme_query: MlmeQueryProxy,
}

pub type PhyMap = WatchableMap<u16, PhyDevice>;
pub type IfaceMap = WatchableMap<u16, IfaceDevice>;

pub async fn serve_phys(phys: Arc<PhyMap>) -> Result<Never, Error> {
    let mut new_phys = device_watch::watch_phy_devices()?;
    let mut active_phys = ConcurrentTasks::new();
    loop {
        let mut new_phy = new_phys.next();
        select! {
            new_phy => match new_phy {
                None => bail!("new phy stream unexpectedly finished"),
                Some(Err(e)) => bail!("new phy stream returned an error: {}", e),
                Some(Ok(new_phy)) => {
                    let fut = serve_phy(&phys, new_phy);
                    active_phys.add(fut);
                }
            },
            active_phys => active_phys.into_any(),
        }
    }
}

async fn serve_phy(phys: &PhyMap, new_phy: device_watch::NewPhyDevice) {
    info!("new phy #{}: {}", new_phy.id, new_phy.device.path().to_string_lossy());
    let id = new_phy.id;
    let event_stream = new_phy.proxy.take_event_stream();
    phys.insert(id, PhyDevice {
        proxy: new_phy.proxy,
        device: new_phy.device,
    });
    let r = await!(event_stream.map_ok(|_| ()).try_collect::<()>());
    phys.remove(&id);
    if let Err(e) = r {
        error!("error reading from the FIDL channel of phy #{}: {}", id, e);
    }
    info!("phy removed: #{}", id);
}

pub async fn serve_ifaces(ifaces: Arc<IfaceMap>, cobalt_sender: CobaltSender) -> Result<Never, Error> {
    let mut new_ifaces = device_watch::watch_iface_devices()?;
    let mut active_ifaces = ConcurrentTasks::new();
    loop {
        let mut new_iface = new_ifaces.next();
        select! {
            new_iface => match new_iface {
                None => bail!("new iface stream unexpectedly finished"),
                Some(Err(e)) => bail!("new iface stream returned an error: {}", e),
                Some(Ok(new_iface)) => {
                    let fut = query_and_serve_iface(new_iface, &ifaces, cobalt_sender.clone());
                    active_ifaces.add(fut);
                }
            },
            active_ifaces => active_ifaces.into_any(),
        }
    }
}

async fn query_and_serve_iface(new_iface: NewIfaceDevice, ifaces: &IfaceMap, cobalt_sender: CobaltSender) {
    let NewIfaceDevice { id, device, proxy } = new_iface;
    let mut event_stream = proxy.take_event_stream();
    let query_resp = match await!(query_iface(proxy.clone(), &mut event_stream)) {
        Ok(x) => x,
        Err(e) => {
            error!("Failed to query new iface '{}': {}", device.path().display(), e);
            return;
        }
    };
    let (stats_sched, stats_reqs) = stats_scheduler::create_scheduler();
    let mlme_query = MlmeQueryProxy::new(proxy.clone());
    let role = query_resp.role;
    let (sme, sme_fut) = match create_sme(proxy, event_stream, query_resp, stats_reqs, cobalt_sender) {
        Ok(x) => x,
        Err(e) => {
            error!("Failed to create SME for new iface '{}': {}",
                   device.path().display(), e);
            return;
        }
    };

    info!("new iface #{} with role '{:?}': {}", id, role, device.path().to_string_lossy());
    ifaces.insert(id, IfaceDevice {
        sme_server: sme,
        stats_sched,
        device,
        mlme_query,
    });

    let r = await!(sme_fut);
    if let Err(e) = r {
        error!("Error serving station for iface #{}: {}", id, e);
    }
    ifaces.remove(&id);
    info!("iface removed: {}", id);
}

async fn query_iface(proxy: fidl_mlme::MlmeProxy, event_stream: &mut MlmeEventStream)
    -> Result<DeviceQueryConfirm, Error>
{
    let query_req = &mut fidl_mlme::DeviceQueryRequest{
        foo: 0,
    };
    proxy.device_query_req(query_req)
        .context("failed to send request to device")?;
    let query_conf = wait_for_query_conf(event_stream);
    pin_mut!(query_conf);
    let mut timeout = Timer::new(5.seconds().after_now());
    select! {
        query_conf => query_conf,
        timeout => bail!("query request timed out"),
    }
}

async fn wait_for_query_conf(event_stream: &mut MlmeEventStream)
    -> Result<DeviceQueryConfirm, Error>
{
    while let Some(event) = await!(event_stream.next()) {
        match event {
            Ok(fidl_mlme::MlmeEvent::DeviceQueryConf { resp }) => return Ok(resp),
            Ok(other) => {
                warn!("Unexpected message from MLME while waiting for \
                               device query response: {:?}", other);
            },
            Err(e) => bail!("error reading from FIDL channel: {}", e),
        }
    }
    return Err(format_err!("device closed the channel before returning query response"));
}

fn create_sme<S>(proxy: fidl_mlme::MlmeProxy,
                 event_stream: fidl_mlme::MlmeEventStream,
                 query_resp: DeviceQueryConfirm,
                 stats_requests: S,
                 cobalt_sender: CobaltSender)
    -> Result<(SmeServer, impl Future<Output = Result<(), Error>>), Error>
    where S: Stream<Item = stats_scheduler::StatsRequest> + Send + Unpin + 'static
{
    let device_info = convert_device_info(&query_resp);
    match query_resp.role {
        fidl_mlme::MacRole::Client => {
            let (sender, receiver) = mpsc::unbounded();
            let fut = station::client::serve(
                proxy, device_info, event_stream, receiver, stats_requests, cobalt_sender);
            Ok((SmeServer::Client(sender), FutureObj::new(Box::new(fut))))
        },
        fidl_mlme::MacRole::Ap => {
            let (sender, receiver) = mpsc::unbounded();
            let fut = station::ap::serve(
                proxy, device_info, event_stream, receiver, stats_requests);
            Ok((SmeServer::Ap(sender), FutureObj::new(Box::new(fut))))
        },
        fidl_mlme::MacRole::Mesh => {
            let (sender, receiver) = mpsc::unbounded();
            let fut = station::mesh::serve(
                proxy, event_stream, receiver, stats_requests);
            Ok((SmeServer::Mesh(sender), FutureObj::new(Box::new(fut))))
        }
    }
}

fn convert_device_info(query_resp: &DeviceQueryConfirm) -> wlan_sme::DeviceInfo {
    let mut supported_channels = HashSet::new();
    for band in &query_resp.bands {
        supported_channels.extend(&band.channels);
    }
    wlan_sme::DeviceInfo {
        supported_channels,
        addr: query_resp.mac_addr,
    }
}
