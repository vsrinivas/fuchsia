// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_hardware_power_statecontrol as reboot;
use fidl_fuchsia_mockrebootcontroller as controller;
use fuchsia_async::{self as fasync, futures::TryStreamExt};
use std::sync::{Arc, Mutex};

pub struct ControllerState {
    reboot_client: Option<reboot::RebootMethodsWatcherProxy>,
}

impl ControllerState {
    pub fn new() -> Arc<Mutex<ControllerState>> {
        Arc::new(Mutex::new(ControllerState { reboot_client: None }))
    }
}

pub fn serve_reboot_server(
    mut stream: reboot::RebootMethodsWatcherRegisterRequestStream,
    state: Arc<Mutex<ControllerState>>,
) {
    fasync::Task::spawn(async move {
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                reboot::RebootMethodsWatcherRegisterRequest::Register {
                    watcher,
                    control_handle: _,
                } => {
                    state.lock().unwrap().reboot_client = Some(watcher.into_proxy().unwrap());
                }
            }
        }
    })
    .detach();
}

pub fn serve_reboot_controller(
    mut stream: controller::MockRebootControllerRequestStream,
    state: Arc<Mutex<ControllerState>>,
) {
    fasync::Task::spawn(async move {
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                controller::MockRebootControllerRequest::TriggerReboot { responder } => {
                    let client_proxy = state.lock().unwrap().reboot_client.take();
                    match client_proxy {
                        Some(proxy) => {
                            match proxy.on_reboot(reboot::RebootReason::UserRequest).await {
                                Err(_) => {
                                    responder
                                        .send(&mut Err(controller::RebootError::ClientError))
                                        .unwrap();
                                }
                                Ok(()) => {
                                    responder.send(&mut Ok(())).unwrap();
                                }
                            }
                        }
                        None => {
                            responder.send(&mut Err(controller::RebootError::NoClientSet)).unwrap();
                            continue;
                        }
                    }
                }
                controller::MockRebootControllerRequest::CrashRebootChannel { responder } => {
                    let client_proxy = state.lock().unwrap().reboot_client.take();
                    match client_proxy {
                        Some(proxy) => {
                            drop(proxy);
                            responder.send(&mut Ok(())).unwrap();
                        }
                        None => {
                            responder.send(&mut Err(controller::RebootError::NoClientSet)).unwrap();
                            continue;
                        }
                    }
                }
            }
        }
    })
    .detach();
}
