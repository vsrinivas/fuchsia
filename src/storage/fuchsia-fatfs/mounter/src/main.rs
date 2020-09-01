// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::RequestStream,
    fidl_fuchsia_fs::{AdminRequestStream, QueryRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_zircon::Status,
    futures::{lock::Mutex, prelude::*},
    std::sync::Arc,
};

mod device;
use crate::device::FatDevice;

/// All the services handled by the fatfs implementation.
pub enum Services {
    Admin(AdminRequestStream),
    Query(QueryRequestStream),
}

pub struct FatServer {
    device: Mutex<Option<FatDevice>>,
}

impl FatServer {
    pub fn new() -> Self {
        FatServer { device: Mutex::new(None) }
    }

    async fn ensure_mounted(&self) -> Result<(), Status> {
        let mut device = self.device.lock().await;
        if device.as_ref().map_or(false, |d| d.is_present()) {
            return Ok(());
        }

        if let Some(device) = device.take() {
            // Clean up the old device.
            device.shut_down().unwrap_or_else(|e| {
                fx_log_info!(
                    "Failed to shut down removed disk (this is probably expected): {:?}",
                    e
                )
            })
        }

        let fat_device = FatDevice::new()
            .await
            .map_err(|e| {
                fx_log_warn!("Failed to create fat device: {:?}. Is it correctly formatted?", e);
                Status::IO
            })?
            .ok_or(Status::UNAVAILABLE)?;

        *device = Some(fat_device);
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
            let device = self.device.lock().await;
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
            let device = self.device.lock().await;
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
            Services::Query(stream) => self.handle_query(stream).await,
            Services::Admin(stream) => self.handle_admin(stream).await,
        }
        .unwrap_or_else(|e| fx_log_err!("{:?}", e));
    }
}

async fn run() -> Result<(), Error> {
    let mut fs: ServiceFs<_> = ServiceFs::new();

    fs.dir("svc").add_fidl_service(Services::Query).add_fidl_service(Services::Admin);
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

#[cfg(test)]
mod test {
    use {
        super::*,
        crate::device::test::{create_ramdisk, format},
        fidl::endpoints::DiscoverableService,
        fidl_fuchsia_fs::{AdminMarker, FilesystemInfoQuery, QueryMarker},
        fuchsia_zircon as zx,
    };

    #[fasync::run_singlethreaded(test)]
    async fn test_multiple_connections() {
        let ramdisk = create_ramdisk();
        let channel = ramdisk.open().expect("Opening ramdisk succeeds");
        format(channel);

        let mut fs = ServiceFs::new();
        fs.add_fidl_service(Services::Query).add_fidl_service(Services::Admin);

        let (svc_dir, remote) = zx::Channel::create().unwrap();
        fs.serve_connection(remote).unwrap();
        let device = Arc::new(FatServer::new());

        let fs_future = fs.for_each_concurrent(10_000, |request| device.handle(request));
        let connection_future = async {
            let (query, remote) = fidl::endpoints::create_proxy::<QueryMarker>().unwrap();
            fdio::service_connect_at(&svc_dir, QueryMarker::SERVICE_NAME, remote.into_channel())
                .expect("Connection to query svc succeeds");

            let (admin, remote) = fidl::endpoints::create_proxy::<AdminMarker>().unwrap();
            fdio::service_connect_at(&svc_dir, AdminMarker::SERVICE_NAME, remote.into_channel())
                .expect("Connection to admin svc succeeds");

            let (_root_dir, remote) =
                fidl::endpoints::create_proxy::<fidl_fuchsia_io::DirectoryMarker>().unwrap();

            // Try sending two requests simultaneously to trigger a race.
            let _info = query.get_info(FilesystemInfoQuery::FsId).await.expect("get_info OK");
            admin.get_root(remote).expect("get_root OK");

            // Drop the connection to the ServiceFs so that the test can complete.
            std::mem::drop(svc_dir);
        };

        futures::join!(fs_future, connection_future);
    }
}
