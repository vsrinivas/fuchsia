// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    anyhow::Error,
    fidl_fuchsia_paver as paver, fuchsia_async as fasync,
    fuchsia_zircon::{Status, Vmo},
    futures::prelude::*,
    parking_lot::Mutex,
    std::{convert::TryInto, sync::Arc},
};

fn verify_and_read_buffer(buffer: &mut fidl_fuchsia_mem::Buffer) -> Vec<u8> {
    // The paver service requires VMOs to be resizable. Assert that the buffer provided by the
    // system updater can be resized without error.
    resize_vmo(&mut buffer.vmo);
    read_mem_buffer(buffer)
}

fn resize_vmo(vmo: &mut Vmo) {
    let size = vmo.get_size().expect("vmo size query to succeed");
    vmo.set_size(size * 2).expect("vmo must be resizable");
}

fn read_mem_buffer(buffer: &fidl_fuchsia_mem::Buffer) -> Vec<u8> {
    let mut res = vec![0; buffer.size.try_into().expect("usize")];
    buffer.vmo.read(&mut res[..], 0).expect("vmo read to succeed");
    res
}

#[derive(Debug, PartialEq, Eq)]
pub enum PaverEvent {
    WriteAsset { configuration: paver::Configuration, asset: paver::Asset, payload: Vec<u8> },
    WriteFirmware { firmware_type: String, payload: Vec<u8> },
    QueryActiveConfiguration,
    SetConfigurationActive { configuration: paver::Configuration },
    SetConfigurationUnbootable { configuration: paver::Configuration },
}

pub struct MockPaverServiceBuilder {
    call_hook: Option<Box<dyn Fn(&PaverEvent) -> Status + Send + Sync>>,
    firmware_hook: Option<Box<dyn Fn(&PaverEvent) -> paver::WriteFirmwareResult + Send + Sync>>,
    active_config: paver::Configuration,
    boot_manager_close_with_epitaph: Option<Status>,
}

impl MockPaverServiceBuilder {
    pub fn new() -> Self {
        Self {
            call_hook: None,
            firmware_hook: None,
            active_config: paver::Configuration::A,
            boot_manager_close_with_epitaph: None,
        }
    }

    pub fn call_hook<F>(mut self, call_hook: F) -> Self
    where
        F: Fn(&PaverEvent) -> Status + Send + Sync + 'static,
    {
        self.call_hook = Some(Box::new(call_hook));
        self
    }

    pub fn firmware_hook<F>(mut self, firmware_hook: F) -> Self
    where
        F: Fn(&PaverEvent) -> paver::WriteFirmwareResult + Send + Sync + 'static,
    {
        self.firmware_hook = Some(Box::new(firmware_hook));
        self
    }

    pub fn active_config(mut self, active_config: paver::Configuration) -> Self {
        self.active_config = active_config;
        self
    }

    pub fn boot_manager_close_with_epitaph(mut self, status: Status) -> Self {
        self.boot_manager_close_with_epitaph = Some(status);
        self
    }

    pub fn build(self) -> MockPaverService {
        let call_hook = self.call_hook.unwrap_or_else(|| Box::new(|_| Status::OK));
        let firmware_hook = self.firmware_hook.unwrap_or_else(|| {
            Box::new(|_| paver::WriteFirmwareResult::Status(Status::OK.into_raw()))
        });

        MockPaverService {
            events: Mutex::new(vec![]),
            call_hook: Box::new(call_hook),
            firmware_hook: Box::new(firmware_hook),
            active_config: self.active_config,
            boot_manager_close_with_epitaph: self.boot_manager_close_with_epitaph,
        }
    }
}

pub struct MockPaverService {
    events: Mutex<Vec<PaverEvent>>,
    call_hook: Box<dyn Fn(&PaverEvent) -> Status + Send + Sync>,
    firmware_hook: Box<dyn Fn(&PaverEvent) -> paver::WriteFirmwareResult + Send + Sync>,
    active_config: paver::Configuration,
    boot_manager_close_with_epitaph: Option<Status>,
}

impl MockPaverService {
    pub fn take_events(&self) -> Vec<PaverEvent> {
        std::mem::replace(&mut *self.events.lock(), vec![])
    }

    async fn run_data_sink_service(
        self: Arc<Self>,
        mut stream: paver::DataSinkRequestStream,
    ) -> Result<(), Error> {
        while let Some(request) = stream.try_next().await? {
            match request {
                paver::DataSinkRequest::WriteAsset {
                    configuration,
                    asset,
                    mut payload,
                    responder,
                } => {
                    let payload = verify_and_read_buffer(&mut payload);
                    let event = PaverEvent::WriteAsset { configuration, asset, payload };
                    let status = (*self.call_hook)(&event);
                    self.events.lock().push(event);
                    responder.send(status.into_raw()).expect("paver response to send");
                }
                paver::DataSinkRequest::WriteFirmware {
                    type_: firmware_type,
                    mut payload,
                    responder,
                } => {
                    let payload = verify_and_read_buffer(&mut payload);
                    let event = PaverEvent::WriteFirmware { firmware_type, payload };
                    let mut result = (*self.firmware_hook)(&event);
                    self.events.lock().push(event);
                    responder.send(&mut result).expect("paver response to send");
                }
                request => panic!("Unhandled method Paver::{}", request.method_name()),
            }
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

        while let Some(request) = stream.try_next().await? {
            match request {
                paver::BootManagerRequest::QueryActiveConfiguration { responder } => {
                    let event = PaverEvent::QueryActiveConfiguration;
                    let status = (*self.call_hook)(&event);
                    self.events.lock().push(event);
                    let mut result = if status == Status::OK {
                        Ok(self.active_config)
                    } else {
                        Err(status.into_raw())
                    };
                    responder.send(&mut result).expect("paver response to send");
                }
                paver::BootManagerRequest::SetConfigurationActive { configuration, responder } => {
                    let event = PaverEvent::SetConfigurationActive { configuration };
                    let status = (*self.call_hook)(&event);
                    self.events.lock().push(event);
                    responder.send(status.into_raw()).expect("paver response to send");
                }
                paver::BootManagerRequest::SetConfigurationUnbootable {
                    configuration,
                    responder,
                } => {
                    let event = PaverEvent::SetConfigurationUnbootable { configuration };
                    let status = (*self.call_hook)(&event);
                    self.events.lock().push(event);
                    responder.send(status.into_raw()).expect("paver response to send");
                }
                request => panic!("Unhandled method Paver::{}", request.method_name()),
            }
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
                    fasync::spawn(
                        paver_service_clone
                            .run_data_sink_service(data_sink.into_stream()?)
                            .unwrap_or_else(|e| panic!("error running data sink service: {:?}", e)),
                    );
                }
                paver::PaverRequest::FindBootManager { boot_manager, .. } => {
                    let paver_service_clone = self.clone();
                    fasync::spawn(
                        paver_service_clone.run_boot_manager_service(boot_manager).unwrap_or_else(
                            |e| panic!("error running boot manager service: {:?}", e),
                        ),
                    );
                }
                request => panic!("Unhandled method Paver::{}", request.method_name()),
            }
        }

        Ok(())
    }
}
