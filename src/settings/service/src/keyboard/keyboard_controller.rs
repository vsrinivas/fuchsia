// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::{SettingInfo, SettingType};
use crate::handler::base::Request;
use crate::handler::setting_handler::persist::{controller as data_controller, ClientProxy};
use crate::handler::setting_handler::{
    controller, ControllerError, IntoHandlerResult, SettingHandlerResult,
};
use crate::keyboard::types::{KeyboardInfo, KeymapId};
use crate::trace;
use settings_storage::device_storage::{DeviceStorage, DeviceStorageCompatible};
use settings_storage::storage_factory::StorageAccess;

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

impl StorageAccess for KeyboardController {
    type Storage = DeviceStorage;
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
                let id = fuchsia_trace::Id::new();
                trace!(id, "set keyboard");
                let mut current = self.client.read_setting::<KeyboardInfo>(id).await;
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
                    keyboard_info.autorepeat.or(current.autorepeat).and_then(|value| {
                        if value.delay == 0 && value.period == 0 {
                            // Clean up Autorepeat when delay and period are set to zero.
                            None
                        } else {
                            Some(value)
                        }
                    });
                Some(self.client.write_setting(current.into(), id).await.into_handler_result())
            }
            Request::Get => {
                let id = fuchsia_trace::Id::new();
                trace!(id, "get keyboard");
                Some(self.client.read_setting_info::<KeyboardInfo>(id).await.into_handler_result())
            }
            _ => None,
        }
    }
}
