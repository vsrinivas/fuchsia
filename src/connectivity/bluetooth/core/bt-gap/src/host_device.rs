// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::format_err,
    fidl_fuchsia_bluetooth::{self as fbt, DeviceClass},
    fidl_fuchsia_bluetooth_control::{self as control, PairingOptions},
    fidl_fuchsia_bluetooth_host::{HostEvent, HostProxy},
    fidl_fuchsia_bluetooth_sys::{self as sys, LeSecurityMode},
    fuchsia_bluetooth::{
        inspect::Inspectable,
        types::{BondingData, HostData, HostInfo, Peer, PeerId},
    },
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn, fx_vlog},
    futures::{Future, FutureExt, StreamExt, TryFutureExt},
    parking_lot::RwLock,
    pin_utils::pin_mut,
    std::{
        convert::TryInto,
        path::PathBuf,
        sync::{Arc, Weak},
    },
};

use crate::types::{self, from_fidl_result, Error};

/// When the host dispatcher requests discovery on a host device, the host device starts discovery
/// and returns a HostDiscoverySession. The state of discovery on the host device persists until
/// this session is dropped.
pub struct HostDiscoverySession {
    adap: Weak<RwLock<HostDevice>>,
}

impl Drop for HostDiscoverySession {
    fn drop(&mut self) {
        fx_vlog!(1, "HostDiscoverySession ended");
        if let Some(host) = self.adap.upgrade() {
            if let Err(err) = host.write().stop_discovery() {
                // TODO(45325) - we should close the host channel if an error is returned
                fx_log_warn!("Unexpected error response when stopping discovery: {:?}", err);
            }
        }
    }
}

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

    pub fn establish_discovery_session(
        host: &Arc<RwLock<HostDevice>>,
    ) -> impl Future<Output = types::Result<HostDiscoverySession>> {
        let token = HostDiscoverySession { adap: Arc::downgrade(host) };
        host.write().host.start_discovery().map(from_fidl_result).map_ok(|_| token)
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
        self.host.add_bonded_devices(&mut bonds.iter_mut()).map(|r| {
            match r {
                Err(fidl_error) => Err(Error::from(fidl_error)),
                // TODO(fxb/44616) - remove when fbt::status is no longer used
                Ok(fbt::Status { error: Some(error) }) => Err(format_err!(
                    "Host Error: {}",
                    error.description.unwrap_or("Unknown Host Error".to_string())
                )
                .into()),
                Ok(_) => Ok(()),
            }
        })
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

    pub fn set_local_data(&self, data: HostData) -> types::Result<()> {
        let mut data = data.into();
        self.host.set_local_data(&mut data).map_err(|e| e.into())
    }

    pub fn enable_privacy(&self, enable: bool) -> types::Result<()> {
        self.host.enable_privacy(enable).map_err(Error::from)
    }

    pub fn enable_background_scan(&self, enable: bool) -> types::Result<()> {
        self.host.enable_background_scan(enable).map_err(Error::from)
    }

    pub fn set_le_security_mode(&self, mode: LeSecurityMode) -> types::Result<()> {
        self.host.set_le_security_mode(mode).map_err(Error::from)
    }

    /// `apply_sys_settings` applies each field present in `settings` to the host device, leaving
    /// omitted parameters unchanged. If present, the `Err` arm of the returned future's output
    /// is the error associated with the first failure to apply a setting to the host device.
    pub fn apply_sys_settings(
        &self,
        settings: &sys::Settings,
    ) -> impl Future<Output = types::Result<()>> {
        let mut error_occurred = settings.le_privacy.map(|en| self.enable_privacy(en)).transpose();
        if let Ok(_) = error_occurred {
            error_occurred =
                settings.le_background_scan.map(|en| self.enable_background_scan(en)).transpose()
        }
        if let Ok(_) = error_occurred {
            error_occurred =
                settings.le_security_mode.map(|m| self.set_le_security_mode(m)).transpose();
        }
        let connectable_fut = error_occurred
            .map(|_| settings.bredr_connectable_mode.map(|c| self.set_connectable(c)));
        async move {
            match connectable_fut {
                Ok(Some(fut)) => fut.await,
                res => res.map(|_| ()),
            }
        }
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

#[cfg(test)]
pub(crate) mod test {
    use super::*;

    use {
        fidl_fuchsia_bluetooth_sys::TechnologyType,
        fuchsia_bluetooth::inspect::placeholder_node,
        fuchsia_bluetooth::types::{Address, HostId},
        std::path::Path,
    };

    pub(crate) fn new_mock(
        id: HostId,
        address: Address,
        path: &Path,
        proxy: HostProxy,
    ) -> HostDevice {
        let info = HostInfo {
            id,
            technology: TechnologyType::DualMode,
            address,
            active: true,
            local_name: None,
            discoverable: false,
            discovering: false,
        };
        HostDevice {
            path: path.into(),
            host: proxy,
            info: Inspectable::new(info, placeholder_node()),
        }
    }
}
