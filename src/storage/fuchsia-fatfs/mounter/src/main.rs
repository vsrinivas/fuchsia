// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::RequestStream,
    fidl_fuchsia_fs::{AdminRequestStream, QueryRequestStream},
    fidl_fuchsia_io::DirectoryRequestStream,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self, fx_log_err, fx_log_warn},
    fuchsia_zircon::{self as zx, Status},
    futures::prelude::*,
    std::sync::{Arc, RwLock},
};

mod device;
use crate::device::FatDevice;

/// All the services handled by the fatfs implementation.
pub enum Services {
    Admin(AdminRequestStream),
    Query(QueryRequestStream),
    // Note this wraps a channel and not a RequestStream so that we can pass it to fatfs's open()
    // call.
    Root(zx::Channel),
}

pub struct FatServer {
    device: RwLock<Option<FatDevice>>,
}

impl FatServer {
    pub fn new() -> Self {
        FatServer { device: RwLock::new(None) }
    }

    async fn ensure_mounted(&self) -> Result<(), Status> {
        // We can't hold the lock across a .await boundary,
        // so we need to acquire it once to check if the device exists, and later to update it.
        {
            let device = self.device.read().unwrap();
            if device.as_ref().map_or(false, |d| d.is_present()) {
                return Ok(());
            }
        };
        let fat_device = FatDevice::new()
            .await
            .map_err(|e| {
                fx_log_warn!("Failed to create fat device: {:?}. Is it correctly formatted?", e);
                Status::IO
            })?
            .ok_or(Status::UNAVAILABLE)?;

        {
            let mut device = self.device.write().unwrap();
            if device.as_ref().map_or(false, |d| d.is_present()) {
                // Lost the race.
                return Ok(());
            }
            *device = Some(fat_device);
        };
        Ok(())
    }

    async fn handle_root(&self, channel: zx::Channel) -> Result<(), Error> {
        match self.ensure_mounted().await {
            Ok(()) => {}
            Err(e) => {
                let stream = DirectoryRequestStream::from_channel(
                    fidl::AsyncChannel::from_channel(channel)?,
                );
                stream.control_handle().shutdown_with_epitaph(e);
                return Ok(());
            }
        };
        let device = self.device.read().unwrap();
        if device.as_ref().map_or(true, |d| !d.is_present()) {
            fx_log_warn!(
                "Device is none: {} is present: {:?}",
                device.is_none(),
                device.as_ref().map(|d| d.is_present())
            );
            // The device disappeared because we lost the race.
            let stream =
                DirectoryRequestStream::from_channel(fidl::AsyncChannel::from_channel(channel)?);
            stream.control_handle().shutdown_with_epitaph(Status::IO_NOT_PRESENT);
            return Ok(());
        }
        device.as_ref().unwrap().open_root(channel);
        Ok(())
    }

    async fn handle_admin(&self, mut stream: AdminRequestStream) -> Result<(), Error> {
        match self.ensure_mounted().await {
            Ok(()) => {}
            Err(e) => {
                stream.control_handle().shutdown_with_epitaph(e);
                return Ok(());
            }
        };

        while let Some(req) = stream.try_next().await? {
            let device = self.device.read().unwrap();
            if device.as_ref().map_or(true, |d| !d.is_present()) {
                // Device has gone away.
                stream.control_handle().shutdown_with_epitaph(Status::IO_NOT_PRESENT);
                break;
            }
            let device = device.as_ref().unwrap();
            device.handle_admin(&device.scope, req)?;
        }
        Ok(())
    }

    async fn handle_query(&self, mut stream: QueryRequestStream) -> Result<(), Error> {
        match self.ensure_mounted().await {
            Ok(()) => {}
            Err(e) => {
                stream.control_handle().shutdown_with_epitaph(e);
                return Ok(());
            }
        };

        while let Some(req) = stream.try_next().await? {
            let device = self.device.read().unwrap();
            if device.as_ref().map_or(true, |d| !d.is_present()) {
                // Device has gone away.
                stream.control_handle().shutdown_with_epitaph(Status::IO_NOT_PRESENT);
                break;
            }
            let device = device.as_ref().unwrap();
            device.handle_query(&device.scope, req)?;
        }
        Ok(())
    }

    pub async fn handle(&self, service: Services) {
        match service {
            Services::Root(chan) => self.handle_root(chan).await,
            Services::Query(stream) => self.handle_query(stream).await,
            Services::Admin(stream) => self.handle_admin(stream).await,
        }
        .unwrap_or_else(|e| fx_log_err!("{:?}", e));
    }
}

async fn run() -> Result<(), Error> {
    let mut fs: ServiceFs<_> = ServiceFs::new();

    fs.dir("svc").add_fidl_service(Services::Query).add_fidl_service(Services::Admin);
    fs.add_service_at("root", |chan| Some(Services::Root(chan)));
    fs.take_and_serve_directory_handle()?;

    let device = Arc::new(FatServer::new());

    const MAX_CONCURRENT: usize = 10_000;
    fs.for_each_concurrent(MAX_CONCURRENT, |request| device.handle(request)).await;

    Ok(())
}

#[fasync::run(10)]
async fn main() {
    fuchsia_syslog::init().unwrap();

    run().await.unwrap_or_else(|e| fx_log_err!("Error while running fatfs mounter: {:?}", e));
}
