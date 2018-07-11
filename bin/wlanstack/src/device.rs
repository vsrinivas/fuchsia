// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async::TimeoutExt;
use device_watch::{self, NewIfaceDevice};
use failure::Error;
use fidl_mlme::{self, DeviceQueryConfirm, MlmeEventStream};
use futures::prelude::*;
use futures::{stream, channel::mpsc};
use station;
use stats_scheduler::{self, StatsScheduler};
use std::collections::HashSet;
use watchable_map::WatchableMap;
use wlan;
use wlan_dev;
use wlan_sme;
use zx::prelude::*;

use std::sync::Arc;

pub struct PhyDevice {
    pub proxy: wlan::PhyProxy,
    pub device: wlan_dev::Device,
}

pub type ClientSmeServer = mpsc::UnboundedSender<super::station::ClientSmeEndpoint>;

pub enum SmeServer {
    Client(ClientSmeServer),
    _Ap,
}

pub struct IfaceDevice {
    pub sme_server: SmeServer,
    pub stats_sched: StatsScheduler,
    pub device: wlan_dev::Device,
}

pub type PhyMap = WatchableMap<u16, PhyDevice>;
pub type IfaceMap = WatchableMap<u16, IfaceDevice>;

pub fn serve_phys(phys: Arc<PhyMap>)
    -> Result<impl Future<Item = (), Error = Error>, Error>
{
    Ok(device_watch::watch_phy_devices()?
        .err_into()
        .chain(stream::once(Err(format_err!("phy watcher stream unexpectedly finished"))))
        .for_each_concurrent(move |new_phy| serve_phy(phys.clone(), new_phy))
        .map(|_| ()))
}

fn serve_phy(phys: Arc<PhyMap>,
             new_phy: device_watch::NewPhyDevice)
    -> impl Future<Item = (), Error = Error>
{
    info!("new phy #{}: {}", new_phy.id, new_phy.device.path().to_string_lossy());
    let id = new_phy.id;
    let event_stream = new_phy.proxy.take_event_stream();
    phys.insert(id, PhyDevice {
        proxy: new_phy.proxy,
        device: new_phy.device,
    });
    event_stream
        .for_each(|_| Ok(()))
        .then(move |r| {
            info!("phy removed: {}", id);
            phys.remove(&id);
            r.map(|_| ()).map_err(|e| e.into())
        })
}

pub fn serve_ifaces(ifaces: Arc<IfaceMap>)
    -> Result<impl Future<Item = (), Error = Error>, Error>
{
    Ok(device_watch::watch_iface_devices()?
        .err_into()
        .chain(stream::once(Err(format_err!("iface watcher stream unexpectedly finished"))))
        .for_each_concurrent(move |new_iface| {
            let ifaces = ifaces.clone();
            query_iface(new_iface)
                .and_then(move |(new_iface, event_stream, query_resp)|
                    serve_iface(ifaces, new_iface, event_stream, query_resp))
                .recover(|e| error!("{}", e))
        })
        .map(|_| ()))
}

fn query_iface(new_iface: NewIfaceDevice)
    -> impl Future<Item = (NewIfaceDevice, MlmeEventStream, DeviceQueryConfirm), Error = Error>
{
    let query_req = &mut fidl_mlme::DeviceQueryRequest{
        foo: 0,
    };
    let event_stream = new_iface.proxy.take_event_stream();
    new_iface.proxy.device_query_req(query_req)
        .into_future()
        .err_into::<Error>()
        .map_err(|e| e.context("Failed to add new iface").into())
        .and_then(move |()|
            event_stream
                .filter_map(|event| Ok(match event {
                    fidl_mlme::MlmeEvent::DeviceQueryConf{ resp } => Some(resp),
                    other => {
                        warn!("Unexpected message from MLME while waiting for \
                               device query response: {:?}", other);
                        None
                    }
                }))
                .next()
                .map_err(|(e, _)| e)
                .err_into::<Error>()
        )
        .on_timeout(5.seconds().after_now(), || Err(format_err!("timed out")))
        .expect("failed to set timeout")
        .then(|r| match r {
            Ok((Some(query_resp), stream)) => {
                let event_stream = stream.into_inner();
                Ok((new_iface, event_stream, query_resp))
            },
            Ok((None, _)) =>
                Err(format_err!("New iface '{}' closed the channel before returning query response",
                        new_iface.device.path().display())),
            Err(e) => Err(e.context(format!("Failed to query new iface '{}'",
                                    new_iface.device.path().display())).into()),
        })
}

fn serve_iface(ifaces: Arc<IfaceMap>,
               new_iface: NewIfaceDevice,
               event_stream: MlmeEventStream,
               query_resp: DeviceQueryConfirm)
    -> impl Future<Item = (), Error = Error>
{
    let NewIfaceDevice{ id, proxy, device } = new_iface;
    let (stats_sched, stats_requests) = stats_scheduler::create_scheduler();
    let ifaces_two = ifaces.clone();
    serve_sme(proxy, event_stream, query_resp, stats_requests)
        .into_future()
        .and_then(move |(sme_server, fut)| {
            info!("new iface #{}: {}", id, device.path().to_string_lossy());
            ifaces.insert(id, IfaceDevice {
                sme_server,
                stats_sched,
                device,
            });
            fut
        })
        .map_err(move |e| e.context(format!("Error serving station for iface #{}", id)).into())
        .then(move |r| {
            info!("iface removed: {}", id);
            ifaces_two.remove(&id);
            r
        })
}

fn serve_sme<S>(proxy: fidl_mlme::MlmeProxy,
                event_stream: fidl_mlme::MlmeEventStream,
                query_resp: DeviceQueryConfirm,
                stats_requests: S)
    -> Result<(SmeServer, impl Future<Item = (), Error = Error>), Error>
    where S: Stream<Item = stats_scheduler::StatsRequest, Error = Never>
{
    let device_info = convert_device_info(&query_resp);
    match query_resp.role {
        fidl_mlme::MacRole::Client => {
            let (sender, receiver) = mpsc::unbounded();
            let fut = station::serve_client_sme(
                proxy, device_info, event_stream, receiver, stats_requests);
            Ok((SmeServer::Client(sender), fut))
        },
        fidl_mlme::MacRole::Ap => {
            Err(format_err!("Access point SME is not implemented"))
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
