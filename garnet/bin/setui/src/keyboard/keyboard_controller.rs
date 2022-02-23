// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::agent::storage::device_storage::{DeviceStorageAccess, DeviceStorageCompatible};
use crate::base::{SettingInfo, SettingType};
use crate::handler::base::Request;
use crate::handler::setting_handler::persist::{controller as data_controller, ClientProxy};
use crate::handler::setting_handler::{
    controller, ControllerError, IntoHandlerResult, SettingHandlerResult,
};
use crate::keyboard::types::{KeyboardInfo, KeymapId};
use crate::trace;

use async_trait::async_trait;

impl DeviceStorageCompatible for KeyboardInfo {
    const KEY: &'static str = "keyboard_info";

    fn default_value() -> Self {
        // The US_QWERTY keymap is the default if no settings are ever applied.
        KeyboardInfo { keymap: Some(KeymapId::UsQwerty), autorepeat: None }
    }
}

impl From<KeyboardInfo> for SettingInfo {
    fn from(info: KeyboardInfo) -> SettingInfo {
        SettingInfo::Keyboard(info)
    }
}

pub struct KeyboardController {
    client: ClientProxy,
}

impl DeviceStorageAccess for KeyboardController {
    const STORAGE_KEYS: &'static [&'static str] = &[KeyboardInfo::KEY];
}

#[async_trait]
impl data_controller::Create for KeyboardController {
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        Ok(KeyboardController { client })
    }
}

#[async_trait]
impl controller::Handle for KeyboardController {
    async fn handle(&self, request: Request) -> Option<SettingHandlerResult> {
        match request {
            Request::SetKeyboardInfo(keyboard_info) => {
                let nonce = fuchsia_trace::generate_nonce();
                trace!(nonce, "set keyboard");
                let mut current = self.client.read_setting::<KeyboardInfo>(nonce).await;
                if !keyboard_info.is_valid() {
                    return Some(Err(ControllerError::InvalidArgument(
                        SettingType::Keyboard,
                        "keyboard".into(),
                        format!("{:?}", keyboard_info).into(),
                    )));
                }
                // Save the value locally.
                current.keymap = keyboard_info.keymap.or(current.keymap);
                current.autorepeat =
                    keyboard_info.autorepeat.or(current.autorepeat).map_or(None, |value| {
                        if value.delay == 0 && value.period == 0 {
                            // Clean up Autorepeat when delay and period are set to zero.
                            None
                        } else {
                            Some(value)
                        }
                    });
                Some(self.client.write_setting(current.into(), nonce).await.into_handler_result())
            }
            Request::Get => {
                let nonce = fuchsia_trace::generate_nonce();
                trace!(nonce, "get keyboard");
                Some(
                    self.client
                        .read_setting_info::<KeyboardInfo>(nonce)
                        .await
                        .into_handler_result(),
                )
            }
            _ => None,
        }
    }
}
