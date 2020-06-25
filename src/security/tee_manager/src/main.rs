// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "128"]

mod device_server;
mod provider_server;

use {
    crate::device_server::serve_passthrough,
    anyhow::{format_err, Context as _, Error},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_hardware_tee::{DeviceConnectorMarker, DeviceConnectorProxy},
    fidl_fuchsia_tee::DeviceMarker,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog as syslog,
    fuchsia_syslog::macros::*,
    fuchsia_vfs_watcher as vfs, fuchsia_zircon as zx,
    futures::{prelude::*, select, stream::FusedStream},
    io_util::{open_directory_in_namespace, OPEN_RIGHT_READABLE},
    std::path::PathBuf,
};

const DEV_TEE_PATH: &str = "/dev/class/tee";

enum IncomingRequest {
    Device(zx::Channel),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    syslog::init_with_tags(&["tee_manager"])?;

    let device_list = enumerate_tee_devices().await?;

    if device_list.len() == 0 {
        return Err(format_err!("No TEE devices found"));
    } else if device_list.len() > 1 {
        // Cannot handle more than one TEE device
        // If this becomes supported, Manager will need to provide a method for clients to
        // enumerate and select a device to connect to.
        return Err(format_err!("Found more than 1 TEE device - this is currently not supported"));
    }

    let dev_connector_proxy =
        open_tee_device_connector(device_list.first().unwrap().to_str().unwrap())?;

    let mut fs = ServiceFs::new_local();
    fs.dir("svc")
        .add_service_at(DeviceMarker::NAME, |channel| Some(IncomingRequest::Device(channel)));
    fs.take_and_serve_directory_handle()?;

    serve(dev_connector_proxy, fs.fuse()).await
}

async fn serve(
    dev_connector_proxy: DeviceConnectorProxy,
    service_stream: impl Stream<Item = IncomingRequest> + FusedStream + Unpin,
) -> Result<(), Error> {
    let mut device_fut = dev_connector_proxy.take_event_stream().into_future();
    let mut service_fut =
        service_stream.for_each_concurrent(None, |request: IncomingRequest| async {
            match request {
                IncomingRequest::Device(channel) => {
                    serve_passthrough(dev_connector_proxy.clone(), channel).await
                }
            }
            .unwrap_or_else(|e| fx_log_err!("{:?}", e));
        });

    select! {
        service_result = service_fut => Ok(service_result),
        _ = device_fut => Err(format_err!("TEE DeviceConnector closed unexpectedly")),
    }
}

async fn enumerate_tee_devices() -> Result<Vec<PathBuf>, Error> {
    let mut device_list = Vec::new();

    let mut watcher = create_watcher(&DEV_TEE_PATH).await?;

    while let Some(msg) = watcher.try_next().await? {
        match msg.event {
            vfs::WatchEvent::EXISTING => {
                device_list.push(PathBuf::new().join(DEV_TEE_PATH).join(msg.filename));
            }
            vfs::WatchEvent::IDLE => {
                break;
            }
            _ => {
                unreachable!("Non-WatchEvent::EXISTING found before WatchEvent::IDLE");
            }
        }
    }
    Ok(device_list)
}

async fn create_watcher(path: &str) -> Result<vfs::Watcher, Error> {
    let dir = open_directory_in_namespace(path, OPEN_RIGHT_READABLE)?;
    let watcher = vfs::Watcher::new(dir).await?;
    Ok(watcher)
}

fn open_tee_device_connector(path: &str) -> Result<DeviceConnectorProxy, Error> {
    let (proxy, server) = fidl::endpoints::create_proxy::<DeviceConnectorMarker>()
        .context("Failed to create TEE DeviceConnectorProxy")?;
    fdio::service_connect(path, server.into_channel())
        .context("Failed to connect to TEE DeviceConnectorProxy")?;
    Ok(proxy)
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl::{endpoints, Error},
        fidl_fuchsia_hardware_tee::DeviceConnectorRequest,
        fidl_fuchsia_io as fio,
        fidl_fuchsia_tee_manager::ProviderProxy,
        fuchsia_zircon::HandleBased,
        fuchsia_zircon_status::Status,
        futures::channel::mpsc,
    };

    fn spawn_device_connector<F>(
        request_handler: impl Fn(DeviceConnectorRequest) -> F + 'static,
    ) -> DeviceConnectorProxy
    where
        F: Future,
    {
        let (proxy, mut stream) = endpoints::create_proxy_and_stream::<DeviceConnectorMarker>()
            .expect("Failed to create DeviceConnector proxy and server.");

        fasync::spawn_local(async move {
            while let Some(request) = stream.try_next().await.unwrap() {
                request_handler(request).await;
            }
        });

        proxy
    }

    fn get_storage(provider_proxy: &ProviderProxy) -> fio::DirectoryProxy {
        let (client_end, server_end) = endpoints::create_endpoints::<fio::DirectoryMarker>()
            .expect("Failed to create fuchsia::io::Directory endpoints");
        assert!(provider_proxy.request_persistent_storage(server_end).is_ok());
        client_end.into_proxy().expect("Failed to convert ClientEnd to DirectoryProxy")
    }

    fn is_directory(node_info: &fio::NodeInfo) -> bool {
        match node_info {
            fio::NodeInfo::Directory { .. } => true,
            _ => false,
        }
    }

    fn is_closed_with_status(error: Error, status: Status) -> bool {
        match error {
            Error::ClientChannelClosed(s) => s == status,
            _ => false,
        }
    }

    async fn is_valid_storage(storage_proxy: &fio::DirectoryProxy) {
        let maybe_node_info = storage_proxy.describe().await;
        assert!(maybe_node_info.is_ok());
        let node_info = maybe_node_info.unwrap();
        assert!(is_directory(&node_info));
    }

    #[fasync::run_singlethreaded(test)]
    async fn connect() {
        let dev_connector = spawn_device_connector(|request| async move {
            match request {
                DeviceConnectorRequest::ConnectTee {
                    service_provider,
                    tee_request,
                    control_handle: _,
                } => {
                    assert!(service_provider.is_some());
                    assert!(!tee_request.channel().is_invalid_handle());

                    let provider_proxy = service_provider
                        .unwrap()
                        .into_proxy()
                        .expect("Failed to convert ClientEnd to ProviderProxy");

                    is_valid_storage(&get_storage(&provider_proxy)).await;

                    tee_request
                        .close_with_epitaph(Status::OK)
                        .expect("Unable to close tee_request");
                }
                _ => {
                    assert!(false);
                }
            }
        });

        let (mut sender, receiver) = mpsc::channel::<IncomingRequest>(1);

        fasync::spawn_local(async move {
            let result = serve(dev_connector, receiver.fuse()).await;
            assert!(result.is_ok(), "{}", result.unwrap_err());
        });

        let (device_client, device_server) = endpoints::create_endpoints::<DeviceMarker>()
            .expect("Failed to create Device endpoints");

        let device_proxy =
            device_client.into_proxy().expect("Failed to convert ClientEnd to DeviceProxy");

        sender
            .try_send(IncomingRequest::Device(device_server.into_channel()))
            .expect("Unable to send Device Request");

        let (result, _) = device_proxy.take_event_stream().into_future().await;
        assert!(is_closed_with_status(result.unwrap().unwrap_err(), Status::OK));
    }

    #[fasync::run_singlethreaded(test)]
    async fn tee_device_closed() {
        let (dev_connector_proxy, dev_connector_server) =
            fidl::endpoints::create_proxy::<DeviceConnectorMarker>()
                .expect("Unable to create DeviceConnectorProxy");
        let (_sender, receiver) = mpsc::channel::<IncomingRequest>(1);

        dev_connector_server
            .close_with_epitaph(Status::PEER_CLOSED)
            .expect("Could not close DeviceConnector ServerEnd");
        let result = serve(dev_connector_proxy, receiver.fuse()).await;
        assert!(result.is_err());
    }
}
