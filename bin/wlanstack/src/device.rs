// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use device_watch;
use failure::Error;
use futures::prelude::*;
use futures::stream;
use futures::channel::mpsc;
use station;
use stats_scheduler::{self, StatsScheduler};
use watchable_map::WatchableMap;
use wlan;
use wlan_dev;

use std::sync::Arc;

pub struct PhyDevice {
    pub proxy: wlan::PhyProxy,
    pub device: wlan_dev::Device,
}

pub type ClientSmeServer = mpsc::UnboundedSender<super::station::ClientSmeEndpoint>;

pub struct IfaceDevice {
    pub client_sme_server: Option<ClientSmeServer>,
    pub stats_sched: StatsScheduler,
    pub _device: wlan_dev::Device,
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
    println!("new phy #{}: {}", new_phy.id, new_phy.device.path().to_string_lossy());
    let id = new_phy.id;
    let event_stream = new_phy.proxy.take_event_stream();
    phys.insert(id, PhyDevice {
        proxy: new_phy.proxy,
        device: new_phy.device,
    });
    event_stream
        .for_each(|_| Ok(()))
        .then(move |r| {
            println!("phy removed: {}", id);
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
        .for_each_concurrent(move |new_iface| serve_iface(ifaces.clone(), new_iface)
            .map_err(|e| e.never_into()))
        .map(|_| ()))
}

fn serve_iface(ifaces: Arc<IfaceMap>,
               new_iface: device_watch::NewIfaceDevice)
    -> impl Future<Item = (), Error = Never>
{
    println!("new iface #{}: {}", new_iface.id, new_iface.device.path().to_string_lossy());
    let id = new_iface.id;
    let (sender, receiver) = mpsc::unbounded();
    let (stats_sched, stats_requests) = stats_scheduler::create_scheduler();
    ifaces.insert(id, IfaceDevice {
        // TODO(gbonik): check the role of the interface instead of assuming it is a client
        client_sme_server: Some(sender),
        stats_sched,
        _device: new_iface.device,
    });
    station::serve_client_sme(new_iface.proxy, receiver, stats_requests)
        .recover::<Never, _>(|e| eprintln!("Error serving client station: {:?}", e))
        .then(move |_| {
            println!("iface removed: {}", id);
            ifaces.remove(&id);
            Ok(())
        })
}
