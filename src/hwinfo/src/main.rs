// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod config;
mod hwinfo_server;

use {
    anyhow::Error,
    config::{BoardInfo, DeviceInfo, ProductInfo},
    fidl_fuchsia_factory::MiscFactoryStoreProviderMarker,
    fidl_fuchsia_hwinfo::{BoardRequestStream, DeviceRequestStream, ProductRequestStream},
    fuchsia_async as fasync,
    fuchsia_component::client::connect_to_service,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self, fx_log_info},
    futures::prelude::*,
    hwinfo_server::{
        spawn_board_info_server, spawn_device_info_server, spawn_product_info_server,
        BoardInfoServer, DeviceInfoServer, ProductInfoServer,
    },
    std::sync::{Arc, RwLock},
};

enum IncomingServices {
    ProductInfo(ProductRequestStream),
    DeviceInfo(DeviceRequestStream),
    BoardInfo(BoardRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["hwinfo"])?;
    fx_log_info!("Initiating Hwinfo Server...");
    let proxy = connect_to_service::<MiscFactoryStoreProviderMarker>()
        .expect("Failed to connect to MiscFactoryStoreProvider service");
    // Loading Device Info
    let device_info = DeviceInfo::load(&proxy).await;
    let locked_device_info = Arc::new(RwLock::new(device_info));
    // Loading Product Info
    let product_info = ProductInfo::load(&proxy).await;
    let locked_product_info = Arc::new(RwLock::new(product_info));
    // Loading Board Info
    let board_info = BoardInfo::load();
    let locked_board_info = Arc::new(RwLock::new(board_info));
    let mut fs = ServiceFs::new();
    fs.dir("svc")
        .add_fidl_service(IncomingServices::ProductInfo)
        .add_fidl_service(IncomingServices::DeviceInfo)
        .add_fidl_service(IncomingServices::BoardInfo);
    fs.take_and_serve_directory_handle()?;
    const CONCURRENT_LIMIT: usize = 100;
    fs.for_each_concurrent(CONCURRENT_LIMIT, move |incoming_service| {
        let device_info_clone = Arc::clone(&locked_device_info);
        let product_info_clone = Arc::clone(&locked_product_info);
        let board_info_clone = Arc::clone(&locked_board_info);
        async move {
            match incoming_service {
                IncomingServices::ProductInfo(stream) => {
                    let server = ProductInfoServer::new(Arc::clone(&product_info_clone));
                    spawn_product_info_server(server, stream);
                }
                IncomingServices::DeviceInfo(stream) => {
                    let server = DeviceInfoServer::new(Arc::clone(&device_info_clone));
                    spawn_device_info_server(server, stream);
                }
                IncomingServices::BoardInfo(stream) => {
                    let server = BoardInfoServer::new(Arc::clone(&board_info_clone));
                    spawn_board_info_server(server, stream);
                }
            }
        }
    })
    .await;
    Ok(())
}
