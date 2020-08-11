// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::handler::base::SettingHandlerResult;
use crate::handler::setting_handler::{controller, ClientProxy, ControllerError};
use crate::switchboard::base::{DeviceInfo, SettingRequest, SettingResponse};
use async_trait::async_trait;
use std::fs;

const BUILD_TAG_FILE_PATH: &str = "/config/build-info/version";

pub struct DeviceController;

#[async_trait]
impl controller::Create for DeviceController {
    /// Creates the controller
    async fn create(_: ClientProxy) -> Result<Self, ControllerError> {
        Ok(Self {})
    }
}

#[async_trait]
impl controller::Handle for DeviceController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingHandlerResult> {
        match request {
            SettingRequest::Get => {
                let contents =
                    fs::read_to_string(BUILD_TAG_FILE_PATH).expect("Could not read build tag file");
                let device_info = DeviceInfo { build_tag: contents.trim().to_string() };

                Some(Ok(Some(SettingResponse::Device(device_info))))
            }
            _ => None,
        }
    }
}
