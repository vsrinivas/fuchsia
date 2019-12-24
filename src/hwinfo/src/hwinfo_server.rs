// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::config::{BoardInfo, DeviceInfo, ProductInfo},
    anyhow::{Context as _, Error},
    fidl_fuchsia_hwinfo::{
        BoardRequest, BoardRequestStream, DeviceRequest, DeviceRequestStream, ProductRequest,
        ProductRequestStream,
    },
    fuchsia_async as fasync,
    futures::prelude::*,
    std::sync::{Arc, RwLock},
};

type DeviceInfoTable = Arc<RwLock<DeviceInfo>>;

type BoardInfoTable = Arc<RwLock<BoardInfo>>;

type ProductInfoTable = Arc<RwLock<ProductInfo>>;

pub struct DeviceInfoServer {
    device_info_table: DeviceInfoTable,
}

impl DeviceInfoServer {
    pub fn new(device_info_table: DeviceInfoTable) -> Self {
        Self { device_info_table }
    }

    pub async fn handle_requests_from_stream(
        &self,
        mut stream: DeviceRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    async fn handle_request(&self, request: DeviceRequest) -> Result<(), Error> {
        match request {
            DeviceRequest::GetInfo { responder } => {
                responder
                    .send(self.device_info_table.read().unwrap().clone().into())
                    .context("error sending response")?;
            }
        };
        Ok(())
    }
}

pub struct BoardInfoServer {
    board_info_table: BoardInfoTable,
}

impl BoardInfoServer {
    pub fn new(board_info_table: BoardInfoTable) -> Self {
        Self { board_info_table }
    }

    pub async fn handle_requests_from_stream(
        &self,
        mut stream: BoardRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    async fn handle_request(&self, request: BoardRequest) -> Result<(), Error> {
        match request {
            BoardRequest::GetInfo { responder } => {
                responder
                    .send(self.board_info_table.read().unwrap().clone().into())
                    .context("error sending response")?;
            }
        };
        Ok(())
    }
}

pub struct ProductInfoServer {
    product_info_table: ProductInfoTable,
}

impl ProductInfoServer {
    pub fn new(product_info_table: ProductInfoTable) -> Self {
        Self { product_info_table }
    }

    pub async fn handle_requests_from_stream(
        &self,
        mut stream: ProductRequestStream,
    ) -> Result<(), Error> {
        while let Some(req) = stream.try_next().await? {
            self.handle_request(req).await?;
        }
        Ok(())
    }

    async fn handle_request(&self, request: ProductRequest) -> Result<(), Error> {
        match request {
            ProductRequest::GetInfo { responder } => {
                responder
                    .send(self.product_info_table.read().unwrap().clone().into())
                    .context("error sending response")?;
            }
        };
        Ok(())
    }
}

pub fn spawn_device_info_server(server: DeviceInfoServer, stream: DeviceRequestStream) {
    fasync::spawn(async move {
        server.handle_requests_from_stream(stream).await.expect("Failed to run device_info service")
    });
}

pub fn spawn_board_info_server(server: BoardInfoServer, stream: BoardRequestStream) {
    fasync::spawn(async move {
        server.handle_requests_from_stream(stream).await.expect("Failed to run board_info service")
    });
}

pub fn spawn_product_info_server(server: ProductInfoServer, stream: ProductRequestStream) {
    fasync::spawn(async move {
        server
            .handle_requests_from_stream(stream)
            .await
            .expect("Failed to run product_info service")
    });
}
