// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl::endpoints::ClientEnd,
    fidl_fuchsia_bluetooth::{self as fbt, DeviceClass},
    fidl_fuchsia_bluetooth_control::{self as control, HostData, PairingOptions},
    fidl_fuchsia_bluetooth_host::{HostEvent, HostProxy},
    fidl_fuchsia_bluetooth_sys::{InputCapability, OutputCapability, PairingDelegateMarker},
    fidl_fuchsia_mem::Buffer,
    fuchsia_bluetooth::{
        inspect::Inspectable,
        types::{BondingData, HostInfo, Peer, PeerId},
    },
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::{Future, FutureExt, StreamExt, TryFutureExt},
    parking_lot::RwLock,
    pin_utils::pin_mut,
    std::{convert::TryInto, path::PathBuf, sync::Arc},
};

use crate::types::{self, from_fidl_result, from_fidl_status, Error};

pub struct HostDevice {
    pub path: PathBuf,
    host: HostProxy,
    info: Inspectable<HostInfo>,
}

// Many HostDevice methods return impl Future rather than being implemented as `async`. This has an
// important behavioral difference in that the function body is triggered immediately when called.
//
// If they were instead declared async, the function body would not be executed until the first time
// the future was polled.
impl HostDevice {
    pub fn new(path: PathBuf, host: HostProxy, info: Inspectable<HostInfo>) -> Self {
        HostDevice { path, host, info }
    }

    pub fn get_host(&self) -> &HostProxy {
        &self.host
    }

    pub fn set_host_pairing_delegate(
        &self,
        input: InputCapability,
        output: OutputCapability,
        delegate: ClientEnd<PairingDelegateMarker>,
    ) {
        let _ = self.host.set_pairing_delegate(input, output, delegate);
    }

    pub fn get_info(&self) -> &HostInfo {
        &self.info
    }

    pub fn set_name(&self, mut name: String) -> impl Future<Output = types::Result<()>> {
        self.host.set_local_name(&mut name).map(from_fidl_result)
    }

    pub fn set_device_class(
        &self,
        mut cod: DeviceClass,
    ) -> impl Future<Output = types::Result<()>> {
        self.host.set_device_class(&mut cod).map(from_fidl_result)
    }

    pub fn start_discovery(&mut self) -> impl Future<Output = types::Result<()>> {
        self.host.start_discovery().map(from_fidl_result)
    }

    pub fn connect(&mut self, id: PeerId) -> impl Future<Output = types::Result<()>> {
        let mut id: fbt::PeerId = id.into();
        self.host.connect(&mut id).map(from_fidl_result)
    }

    pub fn disconnect(&mut self, id: PeerId) -> impl Future<Output = types::Result<()>> {
        let mut id: fbt::PeerId = id.into();
        self.host.disconnect(&mut id).map(from_fidl_result)
    }

    pub fn pair(
        &mut self,
        id: PeerId,
        options: PairingOptions,
    ) -> impl Future<Output = types::Result<()>> {
        let mut id: fbt::PeerId = id.into();
        self.host.pair(&mut id, options).map(from_fidl_result)
    }

    pub fn forget(&mut self, id: PeerId) -> impl Future<Output = types::Result<()>> {
        let mut id: fbt::PeerId = id.into();
        self.host.forget(&mut id).map(from_fidl_result)
    }

    pub fn close(&self) -> types::Result<()> {
        self.host.close().map_err(|e| e.into())
    }

    pub fn restore_bonds(
        &self,
        bonds: Vec<BondingData>,
    ) -> impl Future<Output = types::Result<()>> {
        let mut bonds: Vec<_> = bonds.into_iter().map(control::BondingData::from).collect();
        self.host.add_bonded_devices(&mut bonds.iter_mut()).map(from_fidl_status)
    }

    pub fn set_connectable(&self, value: bool) -> impl Future<Output = types::Result<()>> {
        self.host.set_connectable(value).map(from_fidl_result)
    }

    pub fn stop_discovery(&self) -> types::Result<()> {
        self.host.stop_discovery().map_err(|e| e.into())
    }

    pub fn set_discoverable(&self, discoverable: bool) -> impl Future<Output = types::Result<()>> {
        self.host.set_discoverable(discoverable).map(from_fidl_result)
    }

    pub fn set_local_data(&self, mut data: HostData) -> types::Result<()> {
        self.host.set_local_data(&mut data).map_err(|e| e.into())
    }

    pub fn enable_privacy(&self, enable: bool) -> types::Result<()> {
        self.host.enable_privacy(enable).map_err(Error::from)
    }

    pub fn enable_background_scan(&self, enable: bool) -> types::Result<()> {
        self.host.enable_background_scan(enable).map_err(Error::from)
    }

    pub fn get_inspect_vmo(&self) -> impl Future<Output = types::Result<Buffer>> {
        self.host.get_inspect_vmo().map_err(Error::from)
    }
}

/// A type that can be notified when a Host or the peers it knows about change
///
/// Each of these trait methods returns a future that should be polled to completion. Once that
/// returned future is complete, the target type can be considered to have been notified of the
/// update event. This allows asynchronous notifications such as via an asynchronous msg channel.
///
/// The host takes care to serialize updates so that subsequent notifications are not triggered
/// until the previous future has been completed. This allows a HostListener type to ensure they
/// maintain ordering. If required, the implementation of these methods should ensure that
/// completing the future before sending a new update is sufficient to ensure ordering.
///
/// Since the notifying Host will wait on the completion of the returned futures, HostListeners
/// should not perform heavy work that may block or take an unnecessary length of time. If the
/// implementor needs to perform potentially-blocking work in response to these notifications, that
/// should be done in a separate task or thread that does not block the returned future.
pub trait HostListener {
    /// The return Future type of `on_peer_updated`
    type PeerUpdatedFut: Future<Output = ()>;
    /// The return Future type of `on_peer_removed`
    type PeerRemovedFut: Future<Output = ()>;
    /// The return Future type of `on_new_host_bond`
    type HostBondFut: Future<Output = Result<(), anyhow::Error>>;
    /// The return Future type of `on_host_updated`
    type HostInfoFut: Future<Output = Result<(), anyhow::Error>>;

    /// Indicate that a Peer `Peer` has been added or updated
    fn on_peer_updated(&mut self, peer: Peer) -> Self::PeerUpdatedFut;

    /// Indicate that a Peer identified by `id` has been removed
    fn on_peer_removed(&mut self, id: PeerId) -> Self::PeerRemovedFut;

    /// Indicate that a new bond described by `data` has been made
    fn on_new_host_bond(&mut self, data: BondingData) -> Self::HostBondFut;

    /// Indicate that the Host now has properties described by `info`
    fn on_host_updated(&mut self, info: HostInfo) -> Self::HostInfoFut;
}

// TODO(armansito): It feels odd to expose it only so it is available to test/host_device.rs. It
// might be better to move the host_device tests into this module.
pub async fn refresh_host_info(host: Arc<RwLock<HostDevice>>) -> types::Result<HostInfo> {
    let proxy = host.read().host.clone();
    let info = proxy.watch_state().await?;
    let info: HostInfo = info.try_into()?;
    host.write().info.update(info.clone());
    Ok(info)
}

/// Monitors updates from a bt-host device and notifies `listener`. The returned Future represents
/// a task that never ends in successful operation and only returns in case of a failure to
/// communicate with the bt-host device.
pub async fn watch_events<H: HostListener + Clone>(
    listener: H,
    host: Arc<RwLock<HostDevice>>,
) -> types::Result<()> {
    let handle_fidl = handle_fidl_events(listener.clone(), host.clone());
    let watch_peers = watch_peers(listener.clone(), host.clone());
    let watch_state = watch_state(listener, host);
    pin_mut!(handle_fidl);
    pin_mut!(watch_peers);
    pin_mut!(watch_state);
    futures::select! {
        res1 = handle_fidl.fuse() => res1,
        res2 = watch_peers.fuse() => res2,
        res3 = watch_state.fuse() => res3,
    }
}

async fn handle_fidl_events<H: HostListener>(
    mut listener: H,
    host: Arc<RwLock<HostDevice>>,
) -> types::Result<()> {
    let mut stream = host.read().host.take_event_stream();
    while let Some(event) = stream.next().await {
        match event? {
            HostEvent::OnNewBondingData { data } => {
                fx_log_info!("Received bonding data");
                let data: BondingData = match data.try_into() {
                    Err(e) => {
                        fx_log_err!("Invalid bonding data, ignoring: {:#?}", e);
                        continue;
                    }
                    Ok(data) => data,
                };
                if let Err(e) = listener.on_new_host_bond(data.into()).await {
                    fx_log_err!("Failed to persist bonding data: {:#?}", e);
                }
            }
        };
    }
    Err(types::Error::InternalError(format_err!("Host FIDL event stream terminated")))
}

async fn watch_peers<H: HostListener + Clone>(
    mut listener: H,
    host: Arc<RwLock<HostDevice>>,
) -> types::Result<()> {
    let proxy = host.read().host.clone();
    loop {
        let (updated, removed) = proxy.watch_peers().await?;
        for peer in updated.into_iter() {
            listener.on_peer_updated(peer.try_into()?).await;
        }
        for id in removed.into_iter() {
            listener.on_peer_removed(id.into()).await;
        }
    }
}

async fn watch_state<H: HostListener>(
    mut listener: H,
    host: Arc<RwLock<HostDevice>>,
) -> types::Result<()> {
    loop {
        let info = refresh_host_info(host.clone()).await?;
        listener.on_host_updated(info).await?;
    }
}
