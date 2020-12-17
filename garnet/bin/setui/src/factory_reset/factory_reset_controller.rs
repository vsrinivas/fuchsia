// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::base::SettingInfo;
use crate::call;
use crate::handler::base::{SettingHandlerResult, State};
use crate::handler::device_storage::DeviceStorageCompatible;
use crate::handler::setting_handler::controller::Handle;
use crate::handler::setting_handler::persist::{controller, ClientProxy};
use crate::handler::setting_handler::ControllerError;
use crate::service_context::ExternalServiceProxy;
use crate::switchboard::base::{
    ControllerStateResult, FactoryResetInfo, SettingRequest, SettingType,
};
use async_trait::async_trait;
use fidl_fuchsia_recovery_policy::{DeviceMarker, DeviceProxy};
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use std::sync::Arc;

impl DeviceStorageCompatible for FactoryResetInfo {
    const KEY: &'static str = "factory_reset_info";

    fn default_value() -> Self {
        FactoryResetInfo::new(true)
    }
}

impl Into<SettingInfo> for FactoryResetInfo {
    fn into(self) -> SettingInfo {
        SettingInfo::FactoryReset(self)
    }
}

type FactoryResetHandle = Arc<Mutex<FactoryResetManager>>;

/// Handles the mapping between [`SettingRequest`]s/[`State`] changes and the
/// [`FactoryResetManager`] logic. Wraps an Arc Mutex of the manager so that each field
/// doesn't need to be individually locked within the manager.
///
/// [`SettingRequest`]: crate::switchboard::base::SettingRequest
/// [`State`]: crate::handler::base::State
/// [`FactoryResetManager`]: crate::factory_reset::FactoryResetManager
pub struct FactoryResetController {
    handle: FactoryResetHandle,
}

/// Keeps track of the current state of factory reset, is responsible for persisting that state to
/// disk and notifying the fuchsia.recovery.policy.Device fidl interface of any changes.
pub struct FactoryResetManager {
    client: ClientProxy<FactoryResetInfo>,
    is_local_reset_allowed: bool,
    factory_reset_policy_service: ExternalServiceProxy<DeviceProxy>,
}

impl FactoryResetManager {
    async fn from_client(
        client: ClientProxy<FactoryResetInfo>,
    ) -> Result<FactoryResetHandle, ControllerError> {
        client
            .get_service_context()
            .await
            .lock()
            .await
            .connect::<DeviceMarker>()
            .await
            .map(|factory_reset_policy_service| {
                Arc::new(Mutex::new(Self {
                    client,
                    is_local_reset_allowed: true,
                    factory_reset_policy_service,
                }))
            })
            .map_err(|_| {
                ControllerError::InitFailure("could not connect to brightness service".into())
            })
    }

    async fn restore(&mut self) -> SettingHandlerResult {
        self.restore_reset_state(true).await.map(|_| None)
    }

    async fn restore_reset_state(&mut self, send_event: bool) -> ControllerStateResult {
        let info = self.client.read().await;
        self.is_local_reset_allowed = info.is_local_reset_allowed;
        if send_event {
            call!(self.factory_reset_policy_service =>
                set_is_local_reset_allowed(info.is_local_reset_allowed)
            )
            .map_err(|e| {
                fx_log_err!("Failed to set local reset: {:?}", e);
                ControllerError::ExternalFailure(
                    SettingType::FactoryReset,
                    "factory_reset_policy".into(),
                    "restore_reset_state".into(),
                )
            })?;
        }

        Ok(())
    }

    fn get(&self) -> SettingHandlerResult {
        Ok(Some(SettingInfo::FactoryReset(FactoryResetInfo::new(self.is_local_reset_allowed))))
    }

    async fn set_local_reset_allowed(
        &mut self,
        is_local_reset_allowed: bool,
    ) -> SettingHandlerResult {
        let mut info = self.client.read().await;
        self.is_local_reset_allowed = is_local_reset_allowed;
        info.is_local_reset_allowed = is_local_reset_allowed;
        call!(self.factory_reset_policy_service =>
            set_is_local_reset_allowed(info.is_local_reset_allowed)
        )
        .map_err(|e| {
            fx_log_err!("Failed to set local reset: {:?}", e);
            ControllerError::ExternalFailure(
                SettingType::FactoryReset,
                "factory_reset_policy".into(),
                "set_local_reset_allowed".into(),
            )
        })?;
        self.client.write(info, false).await.map(|_| None)
    }
}

#[async_trait]
impl controller::Create<FactoryResetInfo> for FactoryResetController {
    async fn create(client: ClientProxy<FactoryResetInfo>) -> Result<Self, ControllerError> {
        Ok(Self { handle: FactoryResetManager::from_client(client).await? })
    }
}

#[async_trait]
impl Handle for FactoryResetController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingHandlerResult> {
        match request {
            SettingRequest::Restore => Some(self.handle.lock().await.restore().await),
            SettingRequest::Get => Some(self.handle.lock().await.get()),
            SettingRequest::SetLocalResetAllowed(is_local_reset_allowed) => {
                Some(self.handle.lock().await.set_local_reset_allowed(is_local_reset_allowed).await)
            }
            _ => None,
        }
    }

    async fn change_state(&mut self, state: State) -> Option<ControllerStateResult> {
        match state {
            State::Startup => {
                // Restore the factory reset state locally but do not push to
                // the factory reset policy.
                Some(self.handle.lock().await.restore_reset_state(false).await)
            }
            _ => None,
        }
    }
}
