// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(clippy::all)]

use {
    anyhow::{anyhow, Error},
    async_trait::async_trait,
    fidl_fuchsia_mem::Buffer,
    fidl_fuchsia_paver as paver, fuchsia_async as fasync,
    fuchsia_zircon::{Status, Vmo, VmoOptions},
    futures::lock::Mutex as AsyncMutex,
    futures::{channel::mpsc, prelude::*},
    parking_lot::Mutex,
    std::{convert::TryInto, sync::Arc},
};

fn verify_buffer(buffer: &mut Buffer) {
    // The paver service requires VMOs to be resizable. Assert that the buffer provided by the
    // system updater can be resized without error.
    let size = buffer.vmo.get_size().expect("vmo size query to succeed");
    buffer.vmo.set_size(size * 2).expect("vmo must be resizable");
}

fn read_mem_buffer(buffer: &Buffer) -> Vec<u8> {
    let mut res = vec![0; buffer.size.try_into().expect("usize")];
    buffer.vmo.read(&mut res[..], 0).expect("vmo read to succeed");
    res
}

fn write_mem_buffer(payload: Vec<u8>) -> Buffer {
    let vmo =
        Vmo::create_with_opts(VmoOptions::RESIZABLE, payload.len() as u64).expect("Creating VMO");
    vmo.write(&payload, 0).expect("writing to VMO");
    Buffer { vmo, size: payload.len() as u64 }
}

#[derive(Debug, PartialEq, Eq, Clone)]
pub enum PaverEvent {
    ReadAsset { configuration: paver::Configuration, asset: paver::Asset },
    WriteAsset { configuration: paver::Configuration, asset: paver::Asset, payload: Vec<u8> },
    ReadFirmware { configuration: paver::Configuration, firmware_type: String },
    WriteFirmware { configuration: paver::Configuration, firmware_type: String, payload: Vec<u8> },
    QueryActiveConfiguration,
    QueryConfigurationLastSetActive,
    QueryCurrentConfiguration,
    QueryConfigurationStatus { configuration: paver::Configuration },
    SetConfigurationHealthy { configuration: paver::Configuration },
    SetConfigurationActive { configuration: paver::Configuration },
    SetConfigurationUnbootable { configuration: paver::Configuration },
    BootManagerFlush,
    DataSinkFlush,
}

impl PaverEvent {
    pub fn from_data_sink_request(request: &paver::DataSinkRequest) -> PaverEvent {
        match request {
            paver::DataSinkRequest::WriteAsset { configuration, asset, payload, .. } => {
                PaverEvent::WriteAsset {
                    configuration: configuration.to_owned(),
                    asset: asset.to_owned(),
                    payload: read_mem_buffer(payload),
                }
            }
            paver::DataSinkRequest::WriteFirmware {
                configuration,
                type_: firmware_type,
                payload,
                ..
            } => PaverEvent::WriteFirmware {
                configuration: configuration.to_owned(),
                firmware_type: firmware_type.to_owned(),
                payload: read_mem_buffer(payload),
            },
            paver::DataSinkRequest::Flush { .. } => PaverEvent::DataSinkFlush {},
            paver::DataSinkRequest::ReadAsset { configuration, asset, .. } => {
                PaverEvent::ReadAsset {
                    configuration: configuration.to_owned(),
                    asset: asset.to_owned(),
                }
            }
            paver::DataSinkRequest::ReadFirmware { configuration, type_, .. } => {
                PaverEvent::ReadFirmware {
                    configuration: configuration.to_owned(),
                    firmware_type: type_.to_owned(),
                }
            }
            request => panic!("Unhandled method Paver::{}", request.method_name()),
        }
    }

    pub fn from_boot_manager_request(request: &paver::BootManagerRequest) -> PaverEvent {
        match request {
            paver::BootManagerRequest::QueryActiveConfiguration { .. } => {
                PaverEvent::QueryActiveConfiguration
            }
            paver::BootManagerRequest::QueryConfigurationLastSetActive { .. } => {
                PaverEvent::QueryConfigurationLastSetActive
            }
            paver::BootManagerRequest::QueryCurrentConfiguration { .. } => {
                PaverEvent::QueryCurrentConfiguration
            }
            paver::BootManagerRequest::QueryConfigurationStatus { configuration, .. } => {
                PaverEvent::QueryConfigurationStatus { configuration: configuration.to_owned() }
            }
            paver::BootManagerRequest::SetConfigurationHealthy { configuration, .. } => {
                PaverEvent::SetConfigurationHealthy { configuration: configuration.to_owned() }
            }
            paver::BootManagerRequest::SetConfigurationActive { configuration, .. } => {
                PaverEvent::SetConfigurationActive { configuration: configuration.to_owned() }
            }
            paver::BootManagerRequest::SetConfigurationUnbootable { configuration, .. } => {
                PaverEvent::SetConfigurationUnbootable { configuration: configuration.to_owned() }
            }
            paver::BootManagerRequest::Flush { .. } => PaverEvent::BootManagerFlush,
        }
    }
}

/// A Hook gives tests the opportunity to directly respond to a request, mock-style. If a Hook wants
/// to respond to a request, it should send a response on the request's Responder, and then return
/// None. If the Hook wants to pass the request on to the next Hook, it should do so by returning
/// Some(the_request_it_got).
///
/// Unimplemented methods pass on all requests.
///
/// Responding to requests with the fidl bindings can be kind of unwieldy, so see the `hooks` module
/// for tidier syntax in simpler cases.
#[async_trait]
pub trait Hook: Sync {
    async fn boot_manager(
        &self,
        request: paver::BootManagerRequest,
    ) -> Option<paver::BootManagerRequest> {
        Some(request)
    }

    async fn data_sink(&self, request: paver::DataSinkRequest) -> Option<paver::DataSinkRequest> {
        Some(request)
    }
}

pub mod hooks {
    use super::*;

    /// A Hook for the specific case where you want to return an error. If the callback returns
    /// Status::OK, the Hook will pass the request off to the next Hook. Responds to both
    /// BootManagerRequests and DataSinkRequests.
    pub fn return_error<F>(callback: F) -> ReturnError<F>
    where
        F: Fn(&PaverEvent) -> Status,
    {
        ReturnError(callback)
    }

    pub struct ReturnError<F>(F);

    #[async_trait]
    impl<F> Hook for ReturnError<F>
    where
        F: Fn(&PaverEvent) -> Status + Sync,
    {
        async fn boot_manager(
            &self,
            request: paver::BootManagerRequest,
        ) -> Option<paver::BootManagerRequest> {
            let status = (self.0)(&PaverEvent::from_boot_manager_request(&request));
            if status == Status::OK {
                Some(request)
            } else {
                // Ignore errors from peers closing the channel early
                let _ = match request {
                    paver::BootManagerRequest::QueryActiveConfiguration { responder, .. } => {
                        responder.send(&mut Err(status.into_raw()))
                    }
                    paver::BootManagerRequest::QueryConfigurationLastSetActive {
                        responder,
                        ..
                    } => responder.send(&mut Err(status.into_raw())),
                    paver::BootManagerRequest::QueryCurrentConfiguration { responder, .. } => {
                        responder.send(&mut Err(status.into_raw()))
                    }
                    paver::BootManagerRequest::QueryConfigurationStatus { responder, .. } => {
                        responder.send(&mut Err(status.into_raw()))
                    }
                    paver::BootManagerRequest::SetConfigurationHealthy { responder, .. } => {
                        responder.send(status.into_raw())
                    }
                    paver::BootManagerRequest::SetConfigurationActive { responder, .. } => {
                        responder.send(status.into_raw())
                    }
                    paver::BootManagerRequest::SetConfigurationUnbootable { responder, .. } => {
                        responder.send(status.into_raw())
                    }
                    paver::BootManagerRequest::Flush { responder } => {
                        responder.send(status.into_raw())
                    }
                };
                None
            }
        }

        async fn data_sink(
            &self,
            request: paver::DataSinkRequest,
        ) -> Option<paver::DataSinkRequest> {
            let status = (self.0)(&PaverEvent::from_data_sink_request(&request));
            if status == Status::OK {
                Some(request)
            } else {
                // Ignore errors from peers closing the channel early
                let _ = match request {
                    paver::DataSinkRequest::WriteFirmware { responder, .. } => {
                        responder.send(&mut paver::WriteFirmwareResult::Status(status.into_raw()))
                    }
                    paver::DataSinkRequest::ReadAsset { responder, .. } => {
                        responder.send(&mut Err(status.into_raw()))
                    }
                    paver::DataSinkRequest::WriteAsset { responder, .. } => {
                        responder.send(status.into_raw())
                    }
                    paver::DataSinkRequest::Flush { responder, .. } => {
                        responder.send(status.into_raw())
                    }
                    request => panic!("Unhandled method Paver::{}", request.method_name()),
                };
                None
            }
        }
    }

    /// A Hook for responding to `QueryConfigurationStatus` calls.
    pub fn config_status<F>(callback: F) -> ConfigStatus<F>
    where
        F: Fn(paver::Configuration) -> Result<paver::ConfigurationStatus, Status>,
    {
        ConfigStatus(callback)
    }

    pub struct ConfigStatus<F>(F);

    #[async_trait]
    impl<F> Hook for ConfigStatus<F>
    where
        F: Fn(paver::Configuration) -> Result<paver::ConfigurationStatus, Status> + Sync,
    {
        async fn boot_manager(
            &self,
            request: paver::BootManagerRequest,
        ) -> Option<paver::BootManagerRequest> {
            match request {
                paver::BootManagerRequest::QueryConfigurationStatus {
                    configuration,
                    responder,
                } => {
                    let mut result = (self.0)(configuration).map_err(Status::into_raw);
                    // Ignore errors from peers closing the channel early
                    let _ = responder.send(&mut result);
                    None
                }
                request => Some(request),
            }
        }
    }

    /// A Hook for responding to `WriteFirmware` calls.
    pub fn write_firmware<F>(callback: F) -> WriteFirmware<F>
    where
        F: Fn(
            paver::Configuration,
            /* firmware_type */ String,
            /* payload */ Vec<u8>,
        ) -> paver::WriteFirmwareResult,
    {
        WriteFirmware(callback)
    }

    pub struct WriteFirmware<F>(F);

    #[async_trait]
    impl<F> Hook for WriteFirmware<F>
    where
        F: Fn(
                paver::Configuration,
                /* firmware_type */ String,
                /* payload */ Vec<u8>,
            ) -> paver::WriteFirmwareResult
            + Sync,
    {
        async fn data_sink(
            &self,
            request: paver::DataSinkRequest,
        ) -> Option<paver::DataSinkRequest> {
            match request {
                paver::DataSinkRequest::WriteFirmware {
                    configuration,
                    type_: firmware_type,
                    payload,
                    responder,
                } => {
                    let mut result =
                        (self.0)(configuration, firmware_type, read_mem_buffer(&payload));
                    // Ignore errors from peers closing the channel early
                    let _ = responder.send(&mut result);
                    None
                }
                request => Some(request),
            }
        }
    }

    /// A Hook for responding to `ReadFirmware` calls.
    pub fn read_firmware<F>(callback: F) -> ReadFirmware<F>
    where
        F: Fn(paver::Configuration, String) -> Result<Vec<u8>, Status>,
    {
        ReadFirmware(callback)
    }

    pub struct ReadFirmware<F>(F);

    #[async_trait]
    impl<F> Hook for ReadFirmware<F>
    where
        F: Fn(paver::Configuration, String) -> Result<Vec<u8>, Status> + Sync,
    {
        async fn data_sink(
            &self,
            request: paver::DataSinkRequest,
        ) -> Option<paver::DataSinkRequest> {
            match request {
                paver::DataSinkRequest::ReadFirmware { configuration, type_, responder } => {
                    let mut result = (self.0)(configuration, type_)
                        .map(write_mem_buffer)
                        .map_err(Status::into_raw);
                    // Ignore errors from peers closing the channel early
                    let _ = responder.send(&mut result);
                    None
                }
                request => Some(request),
            }
        }
    }

    /// A Hook for responding to `ReadAsset` calls.
    pub fn read_asset<F>(callback: F) -> ReadAsset<F>
    where
        F: Fn(paver::Configuration, paver::Asset) -> Result<Vec<u8>, Status>,
    {
        ReadAsset(callback)
    }

    pub struct ReadAsset<F>(F);

    #[async_trait]
    impl<F> Hook for ReadAsset<F>
    where
        F: Fn(paver::Configuration, paver::Asset) -> Result<Vec<u8>, Status> + Sync,
    {
        async fn data_sink(
            &self,
            request: paver::DataSinkRequest,
        ) -> Option<paver::DataSinkRequest> {
            match request {
                paver::DataSinkRequest::ReadAsset { configuration, asset, responder } => {
                    let mut result = (self.0)(configuration, asset)
                        .map(write_mem_buffer)
                        .map_err(Status::into_raw);
                    // Ignore errors from peers closing the channel early
                    let _ = responder.send(&mut result);
                    None
                }
                request => Some(request),
            }
        }
    }

    /// A Hook for the specific case where you want to control when each `PaverEvent` is emitted.
    pub fn throttle() -> (ThrottleHook, Throttle) {
        let (send, recv) = mpsc::unbounded();
        (ThrottleHook(AsyncMutex::new(Some(recv))), Throttle(send))
    }

    /// Wrapper type to control how many `PaverEvent`s are unblocked. Dropping the `Throttle` will
    /// permanently release all subsequent `PaverEvent`s.
    pub struct Throttle(mpsc::UnboundedSender<PaverEvent>);

    impl Throttle {
        pub fn emit_next_paver_event(&self, expected_event: &PaverEvent) {
            self.0.unbounded_send(expected_event.clone()).expect("emit paver event");
        }

        pub fn emit_next_paver_events(&self, expected_events: &[PaverEvent]) {
            for event in expected_events.iter() {
                self.emit_next_paver_event(event);
            }
        }
    }

    pub struct ThrottleHook(AsyncMutex<Option<mpsc::UnboundedReceiver<PaverEvent>>>);

    #[async_trait]
    impl Hook for ThrottleHook {
        async fn boot_manager(
            &self,
            request: paver::BootManagerRequest,
        ) -> Option<paver::BootManagerRequest> {
            let mut optional_recv = self.0.lock().await;
            if let Some(recv) = optional_recv.as_mut() {
                if let Some(expected_request) = recv.next().await {
                    assert_eq!(PaverEvent::from_boot_manager_request(&request), expected_request);
                } else {
                    assert!(optional_recv.take().is_some());
                }
            }
            Some(request)
        }

        async fn data_sink(
            &self,
            request: paver::DataSinkRequest,
        ) -> Option<paver::DataSinkRequest> {
            let mut optional_recv = self.0.lock().await;
            if let Some(recv) = optional_recv.as_mut() {
                if let Some(expected_request) = recv.next().await {
                    assert_eq!(PaverEvent::from_data_sink_request(&request), expected_request);
                } else {
                    assert!(optional_recv.take().is_some());
                }
            }
            Some(request)
        }
    }
}

pub struct MockPaverServiceBuilder {
    hooks: Vec<Box<dyn Hook + Send + Sync>>,
    event_hook: Option<Box<dyn Fn(&PaverEvent) + Send + Sync>>,
    active_config: paver::Configuration,
    current_config: paver::Configuration,
    boot_manager_close_with_epitaph: Option<Status>,
}

impl MockPaverServiceBuilder {
    pub fn new() -> Self {
        Self {
            hooks: vec![],
            event_hook: None,
            active_config: paver::Configuration::A,
            current_config: paver::Configuration::A,
            boot_manager_close_with_epitaph: None,
        }
    }

    /// Adds a Hook. Hooks are called in order of insertion.
    pub fn insert_hook(mut self, hook: impl Hook + Send + Sync + 'static) -> Self {
        self.hooks.push(Box::new(hook));
        self
    }

    // Provide a callback which will be called for every paver event.
    // Useful for logging or interaction assertions.
    pub fn event_hook<F>(mut self, event_hook: F) -> Self
    where
        F: Fn(&PaverEvent) + Send + Sync + 'static,
    {
        self.event_hook = Some(Box::new(event_hook));
        self
    }

    pub fn active_config(mut self, active_config: paver::Configuration) -> Self {
        self.active_config = active_config;
        self
    }

    pub fn current_config(mut self, current_config: paver::Configuration) -> Self {
        self.current_config = current_config;
        self
    }

    pub fn boot_manager_close_with_epitaph(mut self, status: Status) -> Self {
        self.boot_manager_close_with_epitaph = Some(status);
        self
    }

    pub fn build(self) -> MockPaverService {
        MockPaverService {
            hooks: self.hooks,
            events: Mutex::new(vec![]),
            event_hook: self.event_hook.unwrap_or_else(|| Box::new(|_| ())),
            active_config: self.active_config,
            current_config: self.current_config,
            boot_manager_close_with_epitaph: self.boot_manager_close_with_epitaph,
        }
    }
}

pub struct MockPaverService {
    hooks: Vec<Box<dyn Hook + Send + Sync>>,
    events: Mutex<Vec<PaverEvent>>,
    event_hook: Box<dyn Fn(&PaverEvent) + Send + Sync>,
    active_config: paver::Configuration,
    current_config: paver::Configuration,
    boot_manager_close_with_epitaph: Option<Status>,
}

impl MockPaverService {
    pub fn take_events(&self) -> Vec<PaverEvent> {
        std::mem::replace(&mut *self.events.lock(), vec![])
    }

    /// Spawns a new task to serve the data sink protocol.
    pub fn spawn_data_sink_service(self: &Arc<Self>) -> paver::DataSinkProxy {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<paver::DataSinkMarker>().unwrap();

        fasync::Task::spawn(
            Arc::clone(self)
                .run_data_sink_service(stream)
                .unwrap_or_else(|e| panic!("error running data sink service: {:#}", anyhow!(e))),
        )
        .detach();

        proxy
    }

    /// Spawns a new task to serve the boot manager protocol.
    pub fn spawn_boot_manager_service(self: &Arc<Self>) -> paver::BootManagerProxy {
        let (proxy, server_end) =
            fidl::endpoints::create_proxy::<paver::BootManagerMarker>().unwrap();

        fasync::Task::spawn(
            Arc::clone(self)
                .run_boot_manager_service(server_end)
                .unwrap_or_else(|e| panic!("error running boot manager service: {:#}", anyhow!(e))),
        )
        .detach();

        proxy
    }

    /// Spawns a new task to serve the paver protocol.
    pub fn spawn_paver_service(self: &Arc<Self>) -> paver::PaverProxy {
        let (proxy, stream) =
            fidl::endpoints::create_proxy_and_stream::<paver::PaverMarker>().unwrap();

        fasync::Task::spawn(
            Arc::clone(self)
                .run_paver_service(stream)
                .unwrap_or_else(|e| panic!("error running paver service: {:#}", anyhow!(e))),
        )
        .detach();

        proxy
    }

    fn push_event(self: &Arc<Self>, event: PaverEvent) {
        (*self.event_hook)(&event);
        self.events.lock().push(event);
    }

    async fn run_data_sink_service(
        self: Arc<Self>,
        mut stream: paver::DataSinkRequestStream,
    ) -> Result<(), Error> {
        'req_stream: while let Some(mut request) = stream.try_next().await? {
            self.push_event(PaverEvent::from_data_sink_request(&request));

            for hook in self.hooks.iter() {
                match hook.data_sink(request).await {
                    Some(r) => request = r,
                    None => continue 'req_stream,
                }
            }

            // Ignore errors from peers closing the channel early
            let _ = match request {
                paver::DataSinkRequest::WriteAsset { mut payload, responder, .. } => {
                    verify_buffer(&mut payload);
                    responder.send(Status::OK.into_raw())
                }
                paver::DataSinkRequest::WriteFirmware { mut payload, responder, .. } => {
                    verify_buffer(&mut payload);
                    responder.send(&mut paver::WriteFirmwareResult::Status(Status::OK.into_raw()))
                }
                paver::DataSinkRequest::Flush { responder } => {
                    responder.send(Status::OK.into_raw())
                }
                paver::DataSinkRequest::ReadAsset { responder, .. } => {
                    responder.send(&mut Ok(write_mem_buffer(vec![])))
                }
                paver::DataSinkRequest::ReadFirmware { responder, .. } => {
                    responder.send(&mut Ok(write_mem_buffer(vec![])))
                }
                request => panic!("Unhandled method Paver::{}", request.method_name()),
            };
        }

        Ok(())
    }

    async fn run_boot_manager_service(
        self: Arc<Self>,
        boot_manager: fidl::endpoints::ServerEnd<paver::BootManagerMarker>,
    ) -> Result<(), Error> {
        if let Some(status) = self.boot_manager_close_with_epitaph {
            boot_manager.close_with_epitaph(status)?;
            return Ok(());
        };

        let mut stream = boot_manager.into_stream()?;

        'req_stream: while let Some(mut request) = stream.try_next().await? {
            self.push_event(PaverEvent::from_boot_manager_request(&request));

            for hook in self.hooks.iter() {
                match hook.boot_manager(request).await {
                    Some(r) => request = r,
                    None => continue 'req_stream,
                }
            }

            // Ignore errors from peers closing the channel early
            let _ = match request {
                paver::BootManagerRequest::QueryActiveConfiguration { responder } => {
                    let mut result = if self.active_config == paver::Configuration::Recovery {
                        Err(Status::NOT_SUPPORTED.into_raw())
                    } else {
                        Ok(self.active_config)
                    };
                    responder.send(&mut result)
                }
                paver::BootManagerRequest::QueryConfigurationLastSetActive { responder } => {
                    // TODO(zyecheng): Implement the mock logic for this API and add tests.
                    responder.send(&mut Err(Status::NOT_SUPPORTED.into_raw()))
                }
                paver::BootManagerRequest::QueryCurrentConfiguration { responder } => {
                    responder.send(&mut Ok(self.current_config))
                }
                paver::BootManagerRequest::QueryConfigurationStatus { responder, .. } => {
                    responder.send(&mut Ok(paver::ConfigurationStatus::Healthy))
                }
                paver::BootManagerRequest::SetConfigurationHealthy {
                    configuration,
                    responder,
                    ..
                } => {
                    // Return an error if the given configuration is `Recovery`.
                    let status = if configuration == paver::Configuration::Recovery {
                        Status::INVALID_ARGS
                    } else {
                        Status::OK
                    };
                    responder.send(status.into_raw())
                }
                paver::BootManagerRequest::SetConfigurationActive { responder, .. } => {
                    responder.send(Status::OK.into_raw())
                }
                paver::BootManagerRequest::SetConfigurationUnbootable { responder, .. } => {
                    responder.send(Status::OK.into_raw())
                }
                paver::BootManagerRequest::Flush { responder } => {
                    responder.send(Status::OK.into_raw())
                }
            };
        }

        Ok(())
    }

    pub async fn run_paver_service(
        self: Arc<Self>,
        mut stream: paver::PaverRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            match request {
                paver::PaverRequest::FindDataSink { data_sink, .. } => {
                    let paver_service_clone = self.clone();
                    fasync::Task::spawn(
                        paver_service_clone
                            .run_data_sink_service(data_sink.into_stream()?)
                            .unwrap_or_else(|e| panic!("error running data sink service: {:?}", e)),
                    )
                    .detach();
                }
                paver::PaverRequest::FindBootManager { boot_manager, .. } => {
                    let paver_service_clone = self.clone();
                    fasync::Task::spawn(
                        paver_service_clone.run_boot_manager_service(boot_manager).unwrap_or_else(
                            |e| panic!("error running boot manager service: {:?}", e),
                        ),
                    )
                    .detach();
                }
                request => panic!("Unhandled method Paver::{}", request.method_name()),
            }
        }

        Ok(())
    }
}

#[cfg(test)]
pub mod tests {
    use {
        super::*,
        assert_matches::assert_matches,
        fidl_fuchsia_paver as paver,
        fuchsia_zircon::{self as zx, VmoOptions},
        futures::task::Poll,
    };

    struct MockPaverForTest {
        pub paver: Arc<MockPaverService>,
        pub data_sink: paver::DataSinkProxy,
        pub boot_manager: paver::BootManagerProxy,
    }

    impl MockPaverForTest {
        pub fn new<F>(f: F) -> Self
        where
            F: FnOnce(MockPaverServiceBuilder) -> MockPaverServiceBuilder,
        {
            let paver = f(MockPaverServiceBuilder::new());
            let paver = Arc::new(paver.build());
            let (proxy, stream) = fidl::endpoints::create_proxy_and_stream::<paver::PaverMarker>()
                .expect("Creating paver endpoints");

            fasync::Task::spawn(
                Arc::clone(&paver)
                    .run_paver_service(stream)
                    .unwrap_or_else(|_| panic!("Failed to run paver")),
            )
            .detach();

            let (data_sink, server) = fidl::endpoints::create_proxy::<paver::DataSinkMarker>()
                .expect("Creating data sink endpoints");
            proxy.find_data_sink(server).expect("Finding data sink");
            let (boot_manager, server) =
                fidl::endpoints::create_proxy::<paver::BootManagerMarker>()
                    .expect("Creating boot manager endpoints");
            proxy.find_boot_manager(server).expect("Finding boot manager");

            MockPaverForTest { paver, data_sink, boot_manager }
        }
    }

    #[fasync::run_singlethreaded(test)]
    pub async fn test_events() -> Result<(), Error> {
        let paver = MockPaverForTest::new(|p| p);
        let data = "hello there".as_bytes();
        let vmo =
            Vmo::create_with_opts(VmoOptions::RESIZABLE, data.len() as u64).expect("Creating VMO");
        vmo.write(data, 0).expect("writing to VMO");
        paver
            .data_sink
            .write_asset(
                paver::Configuration::A,
                paver::Asset::Kernel,
                &mut Buffer { vmo, size: data.len() as u64 },
            )
            .await
            .expect("Writing asset");

        let result = paver
            .boot_manager
            .query_active_configuration()
            .await
            .expect("Querying active configuration")
            .expect("Querying active configuration (2)");
        assert_eq!(result, paver::Configuration::A);
        paver
            .boot_manager
            .set_configuration_active(paver::Configuration::B)
            .await
            .expect("Setting active configuration");

        assert_eq!(
            paver.paver.take_events(),
            vec![
                PaverEvent::WriteAsset {
                    configuration: paver::Configuration::A,
                    asset: paver::Asset::Kernel,
                    payload: data.to_vec()
                },
                PaverEvent::QueryActiveConfiguration,
                PaverEvent::SetConfigurationActive { configuration: paver::Configuration::B },
            ]
        );

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    pub async fn test_hook() -> Result<(), Error> {
        let hook = |_: &PaverEvent| zx::Status::NOT_SUPPORTED;
        let paver = MockPaverForTest::new(|p| p.insert_hook(hooks::return_error(hook)));

        assert_eq!(
            Err(zx::Status::NOT_SUPPORTED.into_raw()),
            paver.boot_manager.query_active_configuration().await?
        );

        assert_eq!(paver.paver.take_events(), vec![PaverEvent::QueryActiveConfiguration]);

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    pub async fn test_config_status_hook() -> Result<(), Error> {
        let config_status_hook =
            |_: paver::Configuration| Ok(paver::ConfigurationStatus::Unbootable);
        let paver =
            MockPaverForTest::new(|p| p.insert_hook(hooks::config_status(config_status_hook)));

        assert_eq!(
            Ok(paver::ConfigurationStatus::Unbootable),
            paver.boot_manager.query_configuration_status(paver::Configuration::A).await?
        );

        assert_eq!(
            paver.paver.take_events(),
            vec![PaverEvent::QueryConfigurationStatus { configuration: paver::Configuration::A }]
        );

        Ok(())
    }

    #[test]
    pub fn test_throttle_hook() -> Result<(), Error> {
        let mut executor = fasync::TestExecutor::new().unwrap();

        let (throttle_hook, throttler) = hooks::throttle();
        let paver = MockPaverForTest::new(|p| p.insert_hook(throttle_hook));

        // Both events are blocked.
        let mut fut0 = paver.boot_manager.query_configuration_status(paver::Configuration::A);
        assert_eq!(executor.run_until_stalled(&mut fut0).map(|fidl| fidl.unwrap()), Poll::Pending);
        let mut fut1 = paver.data_sink.flush();
        assert_eq!(executor.run_until_stalled(&mut fut1).map(|fidl| fidl.unwrap()), Poll::Pending);

        // Since we called query_configuration_status first, the boot_manager method has the lock on
        // the `ThrottleHook`. Therefore, when we unblock the next event, we'll observe that
        // query_configuration_status is unblocked first.
        let () = throttler.emit_next_paver_event(&PaverEvent::QueryConfigurationStatus {
            configuration: paver::Configuration::A,
        });
        assert_eq!(
            executor.run_until_stalled(&mut fut0).map(|fidl| fidl.unwrap()),
            Poll::Ready(Ok(paver::ConfigurationStatus::Healthy))
        );
        assert_eq!(executor.run_until_stalled(&mut fut1).map(|fidl| fidl.unwrap()), Poll::Pending);

        // Unblock the remaining event.
        let () = throttler.emit_next_paver_event(&PaverEvent::DataSinkFlush);
        assert_eq!(
            executor.run_until_stalled(&mut fut1).map(|fidl| fidl.unwrap()),
            Poll::Ready(Status::OK.into_raw())
        );

        // Detach the throttler and observe subsequent requests are unblocked.
        drop(throttler);
        executor.run_singlethreaded(async {
            assert_eq!(
                paver.boot_manager.query_current_configuration().await.unwrap(),
                Ok(paver::Configuration::A)
            );
            assert_matches!(
                paver
                    .data_sink
                    .read_asset(paver::Configuration::A, paver::Asset::Kernel)
                    .await
                    .unwrap(),
                Ok(_)
            );
        });

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    pub async fn test_active_config() -> Result<(), Error> {
        let paver = MockPaverForTest::new(|p| p.active_config(paver::Configuration::B));
        assert_eq!(
            Ok(paver::Configuration::B),
            paver.boot_manager.query_active_configuration().await?
        );
        assert_eq!(paver.paver.take_events(), vec![PaverEvent::QueryActiveConfiguration]);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    pub async fn test_active_config_when_recovery() -> Result<(), Error> {
        let paver = MockPaverForTest::new(|p| p.active_config(paver::Configuration::Recovery));
        assert_eq!(
            Err(Status::NOT_SUPPORTED.into_raw()),
            paver.boot_manager.query_active_configuration().await?
        );
        assert_eq!(paver.paver.take_events(), vec![PaverEvent::QueryActiveConfiguration]);
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    pub async fn test_boot_manager_epitaph() -> Result<(), Error> {
        let paver =
            MockPaverForTest::new(|p| p.boot_manager_close_with_epitaph(zx::Status::NOT_SUPPORTED));

        let result = paver.boot_manager.query_active_configuration().await;
        assert_matches!(
            result,
            Err(fidl::Error::ClientChannelClosed { status: zx::Status::NOT_SUPPORTED, .. })
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    pub async fn test_set_config_a_healthy() -> Result<(), Error> {
        let paver = MockPaverForTest::new(|p| p);
        assert_eq!(
            Status::OK.into_raw(),
            paver.boot_manager.set_configuration_healthy(paver::Configuration::A).await?
        );
        assert_eq!(
            paver.paver.take_events(),
            vec![PaverEvent::SetConfigurationHealthy { configuration: paver::Configuration::A }]
        );
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    pub async fn test_set_recovery_config_healthy() -> Result<(), Error> {
        let paver = MockPaverForTest::new(|p| p);
        assert_eq!(
            Status::INVALID_ARGS.into_raw(),
            paver.boot_manager.set_configuration_healthy(paver::Configuration::Recovery).await?
        );
        assert_eq!(
            paver.paver.take_events(),
            vec![PaverEvent::SetConfigurationHealthy {
                configuration: paver::Configuration::Recovery
            }]
        );
        Ok(())
    }
}
