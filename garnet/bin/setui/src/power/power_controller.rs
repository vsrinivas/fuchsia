// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::registry::base::Command, crate::service_context::ServiceContextHandle,
    crate::switchboard::base::*, anyhow::Error, fuchsia_async as fasync, futures::StreamExt,
};

async fn reboot(service_context_handle: ServiceContextHandle) {
    let device_admin = service_context_handle
        .lock()
        .await
        .connect::<fidl_fuchsia_device_manager::AdministratorMarker>()
        .expect("connected to device manager");

    device_admin.suspend(fidl_fuchsia_device_manager::SUSPEND_FLAG_REBOOT).await.ok();
}

pub fn spawn_power_controller(
    service_context_handle: ServiceContextHandle,
) -> futures::channel::mpsc::UnboundedSender<Command> {
    let (system_handler_tx, mut system_handler_rx) = futures::channel::mpsc::unbounded::<Command>();

    fasync::spawn(async move {
        while let Some(command) = system_handler_rx.next().await {
            match command {
                #[allow(unreachable_patterns)]
                Command::HandleRequest(request, responder) =>
                {
                    #[allow(unreachable_patterns)]
                    match request {
                        SettingRequest::Reboot => {
                            reboot(service_context_handle.clone()).await;
                            responder.send(Ok(None)).ok();
                        }
                        _ => {
                            responder
                                .send(Err(Error::new(SwitchboardError::UnimplementedRequest {
                                    setting_type: SettingType::Power,
                                    request: request,
                                })))
                                .ok();
                        }
                    }
                }
                // Ignore unsupported commands
                _ => {}
            }
        }
    });
    system_handler_tx
}
