// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Context;
use fidl_fuchsia_hardware_power_statecontrol as reboot;
use fidl_fuchsia_mockrebootcontroller as controller;
use fuchsia_async::{
    self as fasync,
    futures::{StreamExt, TryStreamExt},
};
use fuchsia_component::server::ServiceFs;
use std::sync::{Arc, Mutex};

fn main() {
    let mut executor = fasync::LocalExecutor::new().context("Error creating executor").unwrap();

    let mut fs = ServiceFs::new();

    let state: Arc<Mutex<ControllerState>> = ControllerState::new();
    let controller_state = state.clone();

    fs.dir("svc")
        .add_fidl_service(move |stream| {
            serve_mock_reboot_controller(stream, controller_state.clone())
        })
        .add_fidl_service(move |stream| serve_mock_reboot_server(stream, state.clone()));

    fs.take_and_serve_directory_handle().unwrap();

    executor.run_singlethreaded(fs.collect::<()>());
}

struct ControllerState {
    reboot_client: Option<reboot::RebootMethodsWatcherProxy>,
}

impl ControllerState {
    pub fn new() -> Arc<Mutex<ControllerState>> {
        Arc::new(Mutex::new(ControllerState { reboot_client: None }))
    }
}

fn serve_mock_reboot_server(
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

fn serve_mock_reboot_controller(
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
