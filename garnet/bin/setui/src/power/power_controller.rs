// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use async_trait::async_trait;
use {
    crate::registry::setting_handler::{controller, ClientProxy, ControllerError},
    crate::service_context::ServiceContextHandle,
    crate::switchboard::base::*,
};

async fn reboot(service_context_handle: &ServiceContextHandle) {
    let device_admin = service_context_handle
        .lock()
        .await
        .connect::<fidl_fuchsia_device_manager::AdministratorMarker>()
        .await
        .expect("connected to device manager");

    device_admin.suspend(fidl_fuchsia_device_manager::SUSPEND_FLAG_REBOOT).await.ok();
}

pub struct PowerController {
    service_context: ServiceContextHandle,
}

#[async_trait]
impl controller::Create for PowerController {
    async fn create(client: ClientProxy) -> Result<Self, ControllerError> {
        let service_context = client.get_service_context().await;
        Ok(Self { service_context: service_context })
    }
}

#[async_trait]
impl controller::Handle for PowerController {
    async fn handle(&self, request: SettingRequest) -> Option<SettingResponseResult> {
        #[allow(unreachable_patterns)]
        match request {
            SettingRequest::Reboot => {
                reboot(&self.service_context).await;
                return Some(Ok(None));
            }
            _ => return None,
        }
    }
}
