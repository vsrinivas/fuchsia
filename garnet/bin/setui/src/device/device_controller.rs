// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::device::types::DeviceInfo;
use crate::handler::base::Request;
use crate::handler::device_storage::DeviceStorageAccess;
use crate::handler::setting_handler::{
    controller, ClientImpl, ControllerError, SettingHandlerResult,
};
use async_trait::async_trait;
use std::fs;
use std::sync::Arc;

const BUILD_TAG_FILE_PATH: &str = "/config/build-info/version";

pub struct DeviceController;

impl DeviceStorageAccess for DeviceController {
    const STORAGE_KEYS: &'static [&'static str] = &[];
}

#[async_trait]
impl controller::Create for DeviceController {
    /// Creates the controller
    async fn create(_: Arc<ClientImpl>) -> Result<Self, ControllerError> {
        Ok(Self {})
    }
}

#[async_trait]
impl controller::Handle for DeviceController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        match request {
            Request::Get => {
                let contents =
                    fs::read_to_string(BUILD_TAG_FILE_PATH).expect("Could not read build tag file");
                let device_info = DeviceInfo { build_tag: contents.trim().to_string() };

                Some(Ok(Some(device_info.into())))
            }
            _ => None,
        }
    }
}
