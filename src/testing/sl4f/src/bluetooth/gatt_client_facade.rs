// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl;
use fidl::endpoints;
use fidl_fuchsia_bluetooth_gatt2::{
    Characteristic, CharacteristicNotifierMarker, CharacteristicNotifierRequest,
    CharacteristicNotifierRequestStream, ClientEventStream, ClientProxy, Handle, LongReadOptions,
    ReadOptions, RemoteServiceEventStream, RemoteServiceProxy, ServiceHandle, ServiceInfo,
    ShortReadOptions, WriteMode, WriteOptions,
};
use fidl_fuchsia_bluetooth_le::{
    CentralMarker, CentralProxy, ConnectionEventStream, ConnectionOptions, ConnectionProxy, Filter,
    ScanOptions, ScanResultWatcherMarker, ScanResultWatcherProxy,
};
use fuchsia_async as fasync;
use fuchsia_component as app;
use futures::{select, FutureExt, StreamExt};
use parking_lot::RwLock;
use std::collections::HashMap;
use std::str::FromStr;
use std::sync::Arc;
use tracing::*;

use fidl_fuchsia_bluetooth;
use fuchsia_bluetooth::types::{le::Peer, PeerId, Uuid};

use crate::bluetooth::types::{BleScanResponse, SerializableReadByTypeResult};
use crate::common_utils::common::macros::with_line;

#[derive(Debug)]
struct Client {
    proxy: ClientProxy,
    _connection: ConnectionProxy,
    // Cache of services populated by GattClientFacade.list_services() and
    // watch_services_task.
    services: HashMap<u64, ServiceInfo>,
    // Task that processes Client.WatchServices() results. Started during the
    // initial call to GattClientFacade.list_services().
    watch_services_task: Option<fasync::Task<()>>,
    // Task listening for closed events from `proxy` and `_connection`.
    _events_task: fasync::Task<()>,
}

#[derive(Debug)]
struct Central {
    proxy: CentralProxy,
    _event_task: fasync::Task<Result<(), Error>>,
}

#[derive(Debug)]
struct RemoteService {
    proxy: RemoteServiceProxy,
    _event_task: fasync::Task<()>,
    // Map of characteristic IDs to CharacteristicNotifier tasks.
    notifier_tasks: HashMap<u64, fasync::Task<()>>,
    peer_id: PeerId,
    service_id: u64,
}

#[derive(Debug)]
pub struct InnerGattClientFacade {
    active_remote_service: Option<RemoteService>,
    central: Option<Central>,
    scan_results: HashMap<PeerId, Peer>,
    clients: HashMap<PeerId, Client>,
    scan_task: Option<fasync::Task<()>>,
}

/// Perform Gatt Client operations.
///
/// Note this object is shared among all threads created by server.
///
#[derive(Debug)]
pub struct GattClientFacade {
    inner: Arc<RwLock<InnerGattClientFacade>>,
}

impl GattClientFacade {
    pub fn new() -> GattClientFacade {
        GattClientFacade {
            inner: Arc::new(RwLock::new(InnerGattClientFacade {
                active_remote_service: None,
                central: None,
                scan_results: HashMap::new(),
                clients: HashMap::new(),
                scan_task: None,
            })),
        }
    }

    pub async fn stop_scan(&self) -> Result<(), Error> {
        let tag = "GattClientFacade::stop_scan";
        if self.inner.write().scan_task.take().is_some() {
            info!(tag = &with_line!(tag), "Scan stopped");
        } else {
            info!(tag = &with_line!(tag), "No scan was running");
        }
        Ok(())
    }

    pub async fn start_scan(&self, filter: Option<Filter>) -> Result<(), Error> {
        let tag = "GattClientFacade::start_scan";

        self.inner.write().scan_results.clear();

        // Set the central proxy if necessary and start a central_listener
        GattClientFacade::set_central_proxy(self.inner.clone());

        let central = self
            .inner
            .read()
            .central
            .as_ref()
            .ok_or(format_err!("No central proxy created."))?
            .proxy
            .clone();

        let options = ScanOptions {
            filters: Some(vec![filter.unwrap_or(Filter::EMPTY)]),
            ..ScanOptions::EMPTY
        };

        let (watcher_proxy, watcher_server) =
            fidl::endpoints::create_proxy::<ScanResultWatcherMarker>()?;

        // Scan doesn't return until scanning has stopped. We don't care when scanning stops, so we
        // can detach a task to run the scan future.
        let scan_fut = central.scan(options, watcher_server);
        fasync::Task::spawn(async move {
            if let Err(e) = scan_fut.await {
                warn!(tag = &with_line!(tag), "FIDL error during scan: {:?}", e);
            }
        })
        .detach();

        self.inner.write().scan_task = Some(fasync::Task::spawn(
            GattClientFacade::scan_result_watcher_task(self.inner.clone(), watcher_proxy),
        ));

        info!(tag = &with_line!(tag), "Scan started");
        Ok(())
    }

    async fn scan_result_watcher_task(
        inner: Arc<RwLock<InnerGattClientFacade>>,
        watcher_proxy: ScanResultWatcherProxy,
    ) {
        let tag = "GattClientFacade::scan_result_watcher_task";
        let mut event_stream = watcher_proxy.take_event_stream();
        let mut watch_fut = watcher_proxy.watch();
        loop {
            select! {
                  watch_result = watch_fut => {
                     let peers = match watch_result {
                          Ok(peers) => peers,
                          Err(e) => {
                               info!(
                                  tag = &with_line!(tag),
                                   "FIDL error calling ScanResultWatcher::Watch(): {}", e
                               );
                               break;
                           }};
                     for fidl_peer in peers {
                        let peer: Peer = fidl_peer.try_into().unwrap();
                        debug!(tag = &with_line!(tag), "Peer discovered (id: {}, name: {:?})", peer.id, peer.name);
                        inner.write().scan_results.insert(peer.id, peer);
                     }
                     watch_fut = watcher_proxy.watch();
                  },
                  event = event_stream.next() => {
                    if let Some(Err(err)) = event {
                              info!(tag = &with_line!(tag), "ScanResultWatcher error: {:?}", err);
                    }
                    break; // The only events are those that close the protocol.
                  }
            }
        }
        inner.write().scan_task = None;
        info!(tag = &with_line!(tag), "ScanResultWatcher closed");
    }

    async fn active_remote_service_event_task(
        inner: Arc<RwLock<InnerGattClientFacade>>,
        mut event_stream: RemoteServiceEventStream,
    ) {
        let tag = "GattClientFacade::active_remote_service_event_task";
        while let Some(event) = event_stream.next().await {
            match event {
                Ok(_) => {} // There are no events
                Err(e) => {
                    warn!(tag = &with_line!(tag), "RemoteService error: {:?}", e);
                    break;
                }
            }
        }
        info!(tag = &with_line!(tag), "RemoteService closed");
        inner.write().active_remote_service = None;
    }

    pub async fn gattc_connect_to_service(
        &self,
        peer_id: String,
        service_id: u64,
    ) -> Result<(), Error> {
        let tag = "GattClientFacade::gattc_connect_to_service";
        let peer_id = PeerId::from_str(&peer_id)?;

        // Check if the service is already the active service.
        if let Some(service) = self.inner.read().active_remote_service.as_ref() {
            if service.peer_id == peer_id && service.service_id == service_id {
                info!(
                    tag = &with_line!(tag),
                    "Aready connected to service (peer: {}, service: {})", peer_id, service_id
                );
                return Ok(());
            }
        }

        self.inner.write().active_remote_service = None;

        let client_proxy = self.get_client_proxy(peer_id).ok_or_else(|| {
            error!(
                tag = &with_line!(tag),
                "Unable to connect to service {} (not connected to peer {})", service_id, peer_id
            );
            format_err!("Not connected to peer")
        })?;
        let (proxy, server) = endpoints::create_proxy()?;
        client_proxy.connect_to_service(&mut ServiceHandle { value: service_id }, server)?;
        let event_stream = proxy.take_event_stream();
        let event_task = fasync::Task::spawn(GattClientFacade::active_remote_service_event_task(
            self.inner.clone(),
            event_stream,
        ));
        self.inner.write().active_remote_service = Some(RemoteService {
            proxy,
            _event_task: event_task,
            notifier_tasks: HashMap::new(),
            peer_id,
            service_id,
        });
        Ok(())
    }

    pub async fn gattc_discover_characteristics(&self) -> Result<Vec<Characteristic>, Error> {
        let discover_characteristics_fut = self
            .get_remote_service_proxy()
            .ok_or(format_err!("RemoteService proxy not available"))?
            .discover_characteristics();
        discover_characteristics_fut.await.map_err(|_| format_err!("Failed to send message"))
    }

    async fn gattc_write_char_internal(
        &self,
        id: u64,
        offset: u16,
        write_value: Vec<u8>,
        mode: WriteMode,
    ) -> Result<(), Error> {
        let mut handle = Handle { value: id };
        let options =
            WriteOptions { offset: Some(offset), write_mode: Some(mode), ..WriteOptions::EMPTY };
        let write_fut = self
            .get_remote_service_proxy()
            .ok_or(format_err!("No active service"))?
            .write_characteristic(&mut handle, &write_value, options);
        write_fut
            .await
            .map_err(|_| format_err!("Failed to send message"))?
            .map_err(|err| format_err!("Failed to write characteristic: {:?}", err))
    }

    pub async fn gattc_write_char_by_id(&self, id: u64, write_value: Vec<u8>) -> Result<(), Error> {
        self.gattc_write_char_internal(id, 0, write_value, WriteMode::Default).await
    }

    pub async fn gattc_write_long_char_by_id(
        &self,
        id: u64,
        offset: u16,
        write_value: Vec<u8>,
        reliable_mode: bool,
    ) -> Result<(), Error> {
        self.gattc_write_char_internal(
            id,
            offset,
            write_value,
            if reliable_mode { WriteMode::Reliable } else { WriteMode::Default },
        )
        .await
    }

    pub async fn gattc_write_char_by_id_without_response(
        &self,
        id: u64,
        write_value: Vec<u8>,
    ) -> Result<(), Error> {
        self.gattc_write_char_internal(id, 0, write_value, WriteMode::WithoutResponse).await
    }

    async fn gattc_read_char_internal(
        &self,
        id: u64,
        mut options: ReadOptions,
    ) -> Result<Vec<u8>, Error> {
        let mut handle = Handle { value: id };
        let read_fut = self
            .get_remote_service_proxy()
            .ok_or(format_err!("RemoteService proxy not available"))?
            .read_characteristic(&mut handle, &mut options);
        let read_value = read_fut
            .await
            .map_err(|_| format_err!("Failed to send message"))?
            .map_err(|err| format_err!("Failed to read long characteristic: {:?}", err))?;
        Ok(read_value.value.unwrap())
    }

    pub async fn gattc_read_char_by_id(&self, id: u64) -> Result<Vec<u8>, Error> {
        self.gattc_read_char_internal(id, ReadOptions::ShortRead(ShortReadOptions {})).await
    }

    pub async fn gattc_read_long_char_by_id(
        &self,
        id: u64,
        offset: u16,
        max_bytes: u16,
    ) -> Result<Vec<u8>, Error> {
        self.gattc_read_char_internal(
            id,
            ReadOptions::LongRead(LongReadOptions {
                offset: Some(offset),
                max_bytes: Some(max_bytes),
                ..LongReadOptions::EMPTY
            }),
        )
        .await
    }

    pub async fn gattc_read_char_by_type(
        &self,
        raw_uuid: String,
    ) -> Result<Vec<SerializableReadByTypeResult>, Error> {
        let uuid = Uuid::from_str(&raw_uuid)
            .map_err(|e| format_err!("Unable to convert to Uuid: {:?}", e))?;
        let mut fidl_uuid = fidl_fuchsia_bluetooth::Uuid::from(uuid);
        let read_fut = self
            .get_remote_service_proxy()
            .ok_or(format_err!("RemoteService proxy not available"))?
            .read_by_type(&mut fidl_uuid);
        let results = read_fut
            .await
            .map_err(|err| format_err!("FIDL error: {:?}", err))?
            .map_err(|err| format_err!("Failed to read characteristic by type: {:?}", err))?
            .into_iter()
            .filter(|r| r.error.is_none())
            .map(|r| SerializableReadByTypeResult::new(r).unwrap())
            .collect();
        Ok(results)
    }

    async fn gattc_read_desc_internal(
        &self,
        id: u64,
        mut options: ReadOptions,
    ) -> Result<Vec<u8>, Error> {
        let mut handle = Handle { value: id };
        let read_fut = self
            .get_remote_service_proxy()
            .ok_or(format_err!("RemoteService proxy not available"))?
            .read_descriptor(&mut handle, &mut options);
        let read_value = read_fut
            .await
            .map_err(|_| format_err!("Failed to send message"))?
            .map_err(|err| format_err!("Failed to read descriptor: {:?}", err))?;
        Ok(read_value.value.unwrap())
    }

    pub async fn gattc_read_desc_by_id(&self, id: u64) -> Result<Vec<u8>, Error> {
        self.gattc_read_desc_internal(id, ReadOptions::ShortRead(ShortReadOptions {})).await
    }

    pub async fn gattc_read_long_desc_by_id(
        &self,
        id: u64,
        offset: u16,
        max_bytes: u16,
    ) -> Result<Vec<u8>, Error> {
        self.gattc_read_desc_internal(
            id,
            ReadOptions::LongRead(LongReadOptions {
                offset: Some(offset),
                max_bytes: Some(max_bytes),
                ..LongReadOptions::EMPTY
            }),
        )
        .await
    }

    pub async fn gattc_write_desc_by_id(&self, id: u64, write_value: Vec<u8>) -> Result<(), Error> {
        self.gattc_write_long_desc_by_id(id, 0, write_value).await
    }

    pub async fn gattc_write_long_desc_by_id(
        &self,
        id: u64,
        offset: u16,
        write_value: Vec<u8>,
    ) -> Result<(), Error> {
        let mut handle = Handle { value: id };
        let options = WriteOptions { offset: Some(offset), ..WriteOptions::EMPTY };
        let write_fut = self
            .get_remote_service_proxy()
            .ok_or(format_err!("RemoteService proxy not available"))?
            .write_descriptor(&mut handle, &write_value, options);
        write_fut
            .await
            .map_err(|_| format_err!("Failed to send message"))?
            .map_err(|err| format_err!("Failed to write descriptor: {:?}", err))
    }

    async fn notifier_task(
        inner: Arc<RwLock<InnerGattClientFacade>>,
        id: u64,
        mut request_stream: CharacteristicNotifierRequestStream,
    ) {
        let tag = "GattClientFacade::notifier_task";
        while let Ok(event) = request_stream.select_next_some().await {
            match event {
                CharacteristicNotifierRequest::OnNotification { value, responder } => {
                    info!(
                        tag = &with_line!(tag),
                        "Received notification (id: {}, value: {:?})",
                        id,
                        value.value.unwrap()
                    );
                    let _ = responder.send();
                }
            }
        }
        info!(tag = &with_line!(tag), "CharacteristicNotifier closed (id: {})", id);
        inner.write().active_remote_service.as_mut().and_then(|s| s.notifier_tasks.remove(&id));
    }

    pub async fn gattc_toggle_notify_characteristic(
        &self,
        id: u64,
        enable: bool,
    ) -> Result<(), Error> {
        let (register_fut, request_stream) = {
            let mut inner = self.inner.write();
            let service = inner
                .active_remote_service
                .as_mut()
                .ok_or(format_err!("Not connected to a service"))?;

            if !enable {
                service.notifier_tasks.remove(&id);
                return Ok(());
            }
            if service.notifier_tasks.contains_key(&id) {
                return Ok(());
            }

            let (client_end, request_stream) =
                fidl::endpoints::create_request_stream::<CharacteristicNotifierMarker>()?;
            let register_fut = service
                .proxy
                .register_characteristic_notifier(&mut Handle { value: id }, client_end);

            (register_fut, request_stream)
        };
        register_fut
            .await
            .map_err(|e| format_err!("FIDL error: {:?}", e))?
            .map_err(|e| format_err!("Error registering notifier: {:?}", e))?;

        let notifier_task = fasync::Task::spawn(GattClientFacade::notifier_task(
            self.inner.clone(),
            id,
            request_stream,
        ));
        self.inner
            .write()
            .active_remote_service
            .as_mut()
            .ok_or(format_err!("Not connected to a service"))?
            .notifier_tasks
            .insert(id, notifier_task);
        Ok(())
    }

    // Warning: hangs forever if there are no service updates!
    async fn watch_services_and_update_map(
        inner: &Arc<RwLock<InnerGattClientFacade>>,
        peer_id: &PeerId,
    ) -> Result<(), Error> {
        let client_proxy = inner
            .read()
            .clients
            .get(peer_id)
            .ok_or(format_err!("Not connected to peer"))?
            .proxy
            .clone();
        let watch_fut = client_proxy.watch_services(&mut Vec::new().into_iter());
        let (updated, removed) =
            watch_fut.await.map_err(|_| format_err!("FIDL error calling WatchServices()"))?;

        // watch_services() returns a diff from the previous call, so we need to apply the diff to
        // the cached services to get an updated list of services.
        let mut inner = inner.write();
        let services = &mut inner
            .clients
            .get_mut(peer_id)
            .ok_or(format_err!("Not connected to peer"))?
            .services;
        for handle in removed {
            services.remove(&handle.value);
        }
        for svc in updated {
            services.insert(svc.handle.unwrap().value, svc);
        }
        Ok(())
    }

    async fn watch_services_task(inner: Arc<RwLock<InnerGattClientFacade>>, peer_id: PeerId) -> () {
        loop {
            let tag = "GattClientFacade::watch_services_task";
            if let Err(err) =
                GattClientFacade::watch_services_and_update_map(&inner, &peer_id).await
            {
                warn!(tag = &with_line!(tag), "{}", err);
                return;
            }
        }
    }

    pub async fn list_services(&self, id: String) -> Result<Vec<ServiceInfo>, Error> {
        let peer_id = PeerId::from_str(&id).map_err(|_| format_err!("Invalid peer id"))?;

        {
            let inner = self.inner.read();
            let client = inner.clients.get(&peer_id).ok_or(format_err!("Not connected to peer"))?;
            // If watch_services_task has already been started, then client.services has the latest cached list of services and we can simply return then.
            if client.watch_services_task.is_some() {
                return Ok(client.services.iter().map(|(_, svc)| svc.clone()).collect());
            }
        }

        // On the first call to list_services(), we need to get the initial list of services and start a task to get updates.
        GattClientFacade::watch_services_and_update_map(&self.inner, &peer_id).await?;
        let task =
            fasync::Task::spawn(GattClientFacade::watch_services_task(self.inner.clone(), peer_id));
        let mut inner = self.inner.write();
        let client = inner.clients.get_mut(&peer_id).ok_or(format_err!("Not connected to peer"))?;
        client.watch_services_task = Some(task);

        Ok(client.services.iter().map(|(_, svc)| svc.clone()).collect())
    }

    pub fn get_client_proxy(&self, id: PeerId) -> Option<ClientProxy> {
        self.inner.read().clients.get(&id).map(|c| c.proxy.clone())
    }

    async fn central_event_task(inner: Arc<RwLock<InnerGattClientFacade>>) -> Result<(), Error> {
        let tag = "GattClientFacade::central_event_task";

        let stream = inner
            .write()
            .central
            .as_ref()
            .ok_or(format_err!("Central not set"))?
            .proxy
            .take_event_stream();

        stream.map(|_| ()).collect::<()>().await;

        info!(tag = &with_line!(tag), "Central closed");
        inner.write().central.take();
        return Ok(());
    }

    // If no proxy exists, set up central server to listen for events.
    // Otherwise, do nothing.
    pub fn set_central_proxy(inner: Arc<RwLock<InnerGattClientFacade>>) {
        if inner.read().central.is_some() {
            return;
        }
        let proxy = app::client::connect_to_protocol::<CentralMarker>()
            .context("Failed to connect to BLE Central service.")
            .unwrap();
        let event_task = fasync::Task::spawn(GattClientFacade::central_event_task(inner.clone()));
        inner.write().central = Some(Central { proxy, _event_task: event_task });
    }

    async fn connection_event_task(
        inner: Arc<RwLock<InnerGattClientFacade>>,
        mut connection_stream: ConnectionEventStream,
        mut client_stream: ClientEventStream,
        peer_id: PeerId,
    ) {
        let tag = "GattClientFacade::connection_event_task";
        select! {
           _ = connection_stream.next().fuse() => info!(tag = &with_line!(tag) , "Connection to {} closed", peer_id),
           _ = client_stream.next().fuse() => info!(tag = &with_line!(tag), "Client for {} closed", peer_id),
        }
        inner.write().clients.remove(&peer_id);
    }

    pub async fn connect_peripheral(&self, id: String) -> Result<(), Error> {
        let tag = "GattClientFacade::connect_peripheral";
        let peer_id = PeerId::from_str(&id)?;

        if self.inner.read().clients.contains_key(&peer_id) {
            info!(tag = &with_line!(tag), "Already connected to {}", peer_id);
            return Ok(());
        }

        GattClientFacade::set_central_proxy(self.inner.clone());

        let (conn_proxy, conn_server_end) = fidl::endpoints::create_proxy()?;
        let options = ConnectionOptions { bondable_mode: Some(true), ..ConnectionOptions::EMPTY };
        self.inner
            .read()
            .central
            .as_ref()
            .unwrap()
            .proxy
            .connect(&mut peer_id.clone().into(), options, conn_server_end)
            .map_err(|_| format_err!("FIDL error when trying to connect()"))?;

        let (client_proxy, client_server_end) = fidl::endpoints::create_proxy()?;
        conn_proxy.request_gatt_client(client_server_end)?;

        let events_task = fasync::Task::spawn(GattClientFacade::connection_event_task(
            self.inner.clone(),
            conn_proxy.take_event_stream(),
            client_proxy.take_event_stream(),
            peer_id.clone(),
        ));

        self.inner.write().clients.insert(
            peer_id,
            Client {
                proxy: client_proxy,
                _connection: conn_proxy,
                services: HashMap::new(),
                watch_services_task: None,
                _events_task: events_task,
            },
        );

        Ok(())
    }

    pub async fn disconnect_peripheral(&self, id: String) -> Result<(), Error> {
        let peer_id = PeerId::from_str(&id)?;
        self.inner.write().clients.remove(&peer_id);
        Ok(())
    }

    // Return the central proxy
    pub fn get_central_proxy(&self) -> Option<CentralProxy> {
        self.inner.read().central.as_ref().map(|c| c.proxy.clone())
    }

    fn get_remote_service_proxy(&self) -> Option<RemoteServiceProxy> {
        self.inner.read().active_remote_service.as_ref().map(|s| s.proxy.clone())
    }

    // Returns scan responses converted to BleScanResponses
    pub fn get_scan_responses(&self) -> Vec<BleScanResponse> {
        const EMPTY_DEVICE: &str = "";
        let mut devices = Vec::new();
        for (peer_id, peer) in &self.inner.read().scan_results {
            let id = format!("{}", peer_id);
            let name = peer.name.clone().unwrap_or(EMPTY_DEVICE.to_string());
            let connectable = peer.connectable;
            devices.push(BleScanResponse::new(id, name, connectable));
        }
        devices
    }

    pub fn print(&self) {
        let tag = "GattClientFacade::print";
        let inner = self.inner.read();
        info!(
            tag = &with_line!(tag),
            "Central: {:?}, Active Service: {:?}, Scan Results: {:?}, Clients: {:?}",
            inner.central,
            inner.active_remote_service,
            inner.scan_results,
            inner.clients,
        );
    }

    pub fn cleanup(&self) {
        let mut inner = self.inner.write();
        inner.active_remote_service = None;
        inner.central = None;
        inner.scan_results.clear();
        inner.clients.clear();
        inner.scan_task = None;
    }
}
