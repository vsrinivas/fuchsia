// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context},
    fidl_fuchsia_bluetooth::{self as fbt, DeviceClass},
    fidl_fuchsia_bluetooth_host::{HostEvent, HostProxy},
    fidl_fuchsia_bluetooth_sys as sys, fuchsia_async as fasync,
    fuchsia_bluetooth::{
        inspect::Inspectable,
        types::{Address, BondingData, HostData, HostId, HostInfo, Peer, PeerId},
    },
    futures::{future::try_join_all, Future, FutureExt, StreamExt, TryFutureExt},
    log::{error, info, trace, warn},
    parking_lot::RwLock,
    pin_utils::pin_mut,
    std::{
        convert::TryInto,
        path::{Path, PathBuf},
        sync::{Arc, Weak},
    },
};

#[cfg(test)]
use fidl_fuchsia_bluetooth_sys::TechnologyType;

use crate::{
    build_config,
    types::{self, from_fidl_result, Error},
};

/// When the host dispatcher requests discovery on a host device, the host device starts discovery
/// and returns a HostDiscoverySession. The state of discovery on the host device persists until
/// this session is dropped.
pub struct HostDiscoverySession {
    host: Weak<HostDeviceState>,
}

impl Drop for HostDiscoverySession {
    fn drop(&mut self) {
        trace!("HostDiscoverySession ended");
        if let Some(host) = self.host.upgrade() {
            if let Err(err) = host.proxy.stop_discovery() {
                // TODO(fxbug.dev/45325) - we should close the host channel if an error is returned
                warn!("Unexpected error response when stopping discovery: {:?}", err);
            }
        }
    }
}

/// When the host dispatcher requests being discoverable on a host device, the host device enables
/// discoverable and returns a HostDiscoverableSession. The discoverable state on the host device
/// persists until this session is dropped.
pub struct HostDiscoverableSession {
    host: Weak<HostDeviceState>,
}

impl Drop for HostDiscoverableSession {
    fn drop(&mut self) {
        trace!("HostDiscoverableSession ended");
        if let Some(host) = self.host.upgrade() {
            let await_response = host.proxy.set_discoverable(false);
            fasync::Task::spawn(async move {
                if let Err(err) = await_response.await {
                    // TODO(fxbug.dev/45325) - we should close the host channel if an error is returned
                    warn!("Unexpected error response when disabling discoverable: {:?}", err);
                }
            })
            .detach();
        }
    }
}

#[derive(Clone)]
pub struct HostDevice(Arc<HostDeviceState>);

pub struct HostDeviceState {
    device_path: PathBuf,
    proxy: HostProxy,
    info: RwLock<Inspectable<HostInfo>>,
}

/// A type for easy debug printing of the main identifiers for the host device, namely: The device
/// path, the host address and the host id.
#[derive(Clone, Debug)]
pub struct HostDebugIdentifiers {
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    id: HostId,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    address: Address,
    // TODO(fxbug.dev/84729)
    #[allow(unused)]
    path: PathBuf,
}

// Many HostDevice methods return impl Future rather than being implemented as `async`. This has an
// important behavioral difference in that the function body is triggered immediately when called.
//
// If they were instead declared async, the function body would not be executed until the first time
// the future was polled.
impl HostDevice {
    pub fn new(device_path: PathBuf, proxy: HostProxy, info: Inspectable<HostInfo>) -> Self {
        HostDevice(Arc::new(HostDeviceState { device_path, proxy, info: RwLock::new(info) }))
    }

    pub fn proxy(&self) -> &HostProxy {
        &self.0.proxy
    }

    pub fn info(&self) -> HostInfo {
        self.0.info.read().clone()
    }

    pub fn id(&self) -> HostId {
        self.0.info.read().id.into()
    }

    pub fn address(&self) -> Address {
        self.0.info.read().address
    }

    /// Convenience method to produce a type for easy debug printing of the main identifiers for the
    /// host device, namely: The device path, the host address and the host id.
    pub fn debug_identifiers(&self) -> HostDebugIdentifiers {
        HostDebugIdentifiers {
            id: self.id(),
            address: self.address(),
            path: self.path().to_path_buf(),
        }
    }

    pub fn path(&self) -> &Path {
        &self.0.device_path
    }

    pub fn set_name(&self, mut name: String) -> impl Future<Output = types::Result<()>> {
        self.0.proxy.set_local_name(&mut name).map(from_fidl_result)
    }

    pub fn set_device_class(&self, mut dc: DeviceClass) -> impl Future<Output = types::Result<()>> {
        self.0.proxy.set_device_class(&mut dc).map(from_fidl_result)
    }

    pub fn establish_discovery_session(
        &self,
    ) -> impl Future<Output = types::Result<HostDiscoverySession>> {
        let token = HostDiscoverySession { host: Arc::downgrade(&self.0) };
        self.0.proxy.start_discovery().map(from_fidl_result).map_ok(|_| token)
    }

    pub fn connect(&self, id: PeerId) -> impl Future<Output = types::Result<()>> {
        let mut id: fbt::PeerId = id.into();
        self.0.proxy.connect(&mut id).map(from_fidl_result)
    }

    pub fn disconnect(&self, id: PeerId) -> impl Future<Output = types::Result<()>> {
        let mut id: fbt::PeerId = id.into();
        self.0.proxy.disconnect(&mut id).map(from_fidl_result)
    }

    pub fn pair(
        &self,
        id: PeerId,
        options: sys::PairingOptions,
    ) -> impl Future<Output = types::Result<()>> {
        let mut id: fbt::PeerId = id.into();
        self.0.proxy.pair(&mut id, options).map(from_fidl_result)
    }

    pub fn forget(&self, id: PeerId) -> impl Future<Output = types::Result<()>> {
        let mut id: fbt::PeerId = id.into();
        self.0.proxy.forget(&mut id).map(from_fidl_result)
    }

    pub fn close(&self) -> types::Result<()> {
        self.0.proxy.close().map_err(|e| e.into())
    }

    pub fn restore_bonds(
        &self,
        bonds: Vec<BondingData>,
    ) -> impl Future<Output = types::Result<Vec<sys::BondingData>>> {
        // TODO(fxbug.dev/80564): Due to the maximum message size, the RestoreBonds call has an
        // upper limit on the number of bonds that may be restored at once. However, this limit is
        // based on the complexity of fields packed into sys::BondingData, which can be measured
        // dynamically with measure-tape as the vector is built. Maximizing the number of bonds per
        // call may improve performance. Instead, a conservative fixed maximum is chosen here (at
        // this time, the maximum message size is 64 KiB and a fully-populated BondingData without
        // peer service records is close to 700 B).
        const MAX_BONDS_PER_RESTORE_BONDS: usize = 16;
        let bonds_chunks = bonds.chunks(MAX_BONDS_PER_RESTORE_BONDS);

        // Bonds that can't be restored are sent back in Ok(Vec<_>), which would not cause
        // try_join_all to bail early
        try_join_all(bonds_chunks.map(|c| {
            self.0
                .proxy
                .restore_bonds(&mut c.into_iter().cloned().map(sys::BondingData::from))
                .map_err(|e| e.into())
        }))
        .map_ok(|v| v.into_iter().flatten().collect())
    }

    pub fn set_connectable(&self, value: bool) -> impl Future<Output = types::Result<()>> {
        self.0.proxy.set_connectable(value).map(from_fidl_result)
    }

    pub fn establish_discoverable_session(
        &self,
    ) -> impl Future<Output = types::Result<HostDiscoverableSession>> {
        let host = Arc::downgrade(&self.0);
        self.0
            .proxy
            .set_discoverable(true)
            .map(from_fidl_result)
            .map_ok(move |_| HostDiscoverableSession { host })
    }

    pub fn set_local_data(&self, data: HostData) -> types::Result<()> {
        self.0.proxy.set_local_data(data.into()).map_err(|e| e.into())
    }

    pub fn enable_privacy(&self, enable: bool) -> types::Result<()> {
        self.0.proxy.enable_privacy(enable).map_err(Error::from)
    }

    pub fn enable_background_scan(&self, enable: bool) -> types::Result<()> {
        self.0.proxy.enable_background_scan(enable).map_err(Error::from)
    }

    pub fn set_le_security_mode(&self, mode: sys::LeSecurityMode) -> types::Result<()> {
        self.0.proxy.set_le_security_mode(mode).map_err(Error::from)
    }

    pub fn apply_config(
        &self,
        config: build_config::Config,
    ) -> impl Future<Output = types::Result<()>> {
        let equivalent_settings = config.into();
        self.apply_sys_settings(&equivalent_settings)
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

    /// Monitors updates from a bt-host device and notifies `listener`. The returned Future represents
    /// a task that never ends in successful operation and only returns in case of a failure to
    /// communicate with the bt-host device.
    pub async fn watch_events<H: HostListener + Clone>(self, listener: H) -> anyhow::Result<()> {
        let handle_fidl = self.clone().handle_fidl_events(listener.clone());
        let watch_peers = self.clone().watch_peers(listener.clone());
        let watch_state = self.watch_state(listener);
        pin_mut!(handle_fidl);
        pin_mut!(watch_peers);
        pin_mut!(watch_state);
        futures::select! {
            res1 = handle_fidl.fuse() => res1.context("failed to handle fuchsia.bluetooth.Host event"),
            res2 = watch_peers.fuse() => res2.context("failed to relay peer watcher from Host"),
            res3 = watch_state.fuse() => res3.context("failed to watch Host for HostInfo"),
        }
    }

    async fn watch_peers<H: HostListener + Clone>(self, mut listener: H) -> types::Result<()> {
        let proxy = self.0.proxy.clone();
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

    async fn watch_state<H: HostListener>(self, mut listener: H) -> types::Result<()> {
        loop {
            let info = self.clone().refresh_host_info().await?;
            listener.on_host_updated(info).await?;
        }
    }

    async fn handle_fidl_events<H: HostListener>(self, mut listener: H) -> types::Result<()> {
        let mut stream = self.0.proxy.take_event_stream();
        while let Some(event) = stream.next().await {
            match event? {
                HostEvent::OnNewBondingData { data } => {
                    info!("Received bonding data");
                    let data: BondingData = match data.try_into() {
                        Err(e) => {
                            error!("Invalid bonding data, ignoring: {:#?}", e);
                            continue;
                        }
                        Ok(data) => data,
                    };
                    if let Err(e) = listener.on_new_host_bond(data.into()).await {
                        error!("Failed to persist bonding data: {:#?}", e);
                    }
                }
            };
        }
        Err(types::Error::InternalError(format_err!("Host FIDL event stream terminated")))
    }

    async fn refresh_host_info(self) -> types::Result<HostInfo> {
        let proxy = self.0.proxy.clone();
        let info = proxy.watch_state().await?;
        let info: HostInfo = info.try_into()?;
        self.0.info.write().update(info.clone());
        Ok(info)
    }

    #[cfg(test)]
    pub(crate) async fn refresh_test_host_info(self) -> types::Result<HostInfo> {
        self.refresh_host_info().await
    }

    #[cfg(test)]
    pub(crate) fn mock_from_id(
        id: HostId,
    ) -> (fidl_fuchsia_bluetooth_host::HostRequestStream, HostDevice) {
        let (host_proxy, host_stream) =
            fidl::endpoints::create_proxy_and_stream::<fidl_fuchsia_bluetooth_host::HostMarker>()
                .unwrap();
        let id_val = id.0 as u8;
        let address = Address::Public([id_val; 6]);
        let path = format!("/dev/host{}", id_val);
        let host_device = HostDevice::mock(id, address, Path::new(&path), host_proxy);
        (host_stream, host_device)
    }

    #[cfg(test)]
    pub(crate) fn mock(id: HostId, address: Address, path: &Path, proxy: HostProxy) -> HostDevice {
        let info = HostInfo {
            id,
            technology: TechnologyType::DualMode,
            address,
            active: false,
            local_name: None,
            discoverable: false,
            discovering: false,
            addresses: vec![address],
        };
        HostDevice::new(
            path.into(),
            proxy,
            Inspectable::new(info, fuchsia_inspect::Node::default()),
        )
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

#[cfg(test)]
pub(crate) mod test {
    use super::*;

    use {
        fidl::endpoints::Responder,
        fidl_fuchsia_bluetooth_host::{HostRequest, HostRequestStream},
        fidl_fuchsia_bluetooth_sys::HostInfo as FidlHostInfo,
        futures::{future, TryStreamExt},
    };

    /// Runs a HostRequestStream that handles StartDiscovery, StopDiscovery, & WatchState requests.
    pub(crate) async fn run_discovery_host_server(
        server: HostRequestStream,
        host_info: Arc<RwLock<HostInfo>>,
    ) -> Result<(), anyhow::Error> {
        server
            .try_for_each(move |req| {
                info!("Handling {:?} in discovery host server", req);
                match req {
                    HostRequest::StartDiscovery { responder } => {
                        assert!(!host_info.read().discovering);
                        host_info.write().discovering = true;
                        assert_matches::assert_matches!(responder.send(&mut Ok(())), Ok(()));
                    }
                    HostRequest::StopDiscovery { control_handle: _ } => {
                        assert!(host_info.read().discovering);
                        host_info.write().discovering = false;
                    }
                    HostRequest::WatchState { responder } => {
                        assert_matches::assert_matches!(
                            responder.send(FidlHostInfo::from(host_info.read().clone())),
                            Ok(())
                        );
                    }
                    HostRequest::WatchPeers { responder, .. } => {
                        info!("Got watch peers, never responding..");
                        responder.drop_without_shutdown();
                    }
                    x => panic!("Unexpected request in discovery host server: {:?}", x),
                }
                future::ok(())
            })
            .await
            .map_err(|e| e.into())
    }
}
