// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_hardware_power_statecontrol as reboot;
use fidl_fuchsia_mockrebootcontroller as controller;
use fuchsia_async as fasync;
use futures::{channel::mpsc, lock::Mutex, SinkExt, StreamExt, TryStreamExt};
use std::sync::Arc;

pub fn serve_reboot_server(
    mut stream: reboot::RebootMethodsWatcherRegisterRequestStream,
    mut proxy_sender: mpsc::Sender<reboot::RebootMethodsWatcherProxy>,
) {
    fasync::Task::spawn(async move {
        while let Some(req) = stream.try_next().await.unwrap() {
            match req {
                reboot::RebootMethodsWatcherRegisterRequest::Register {
                    watcher,
                    control_handle: _,
                } => {
                    proxy_sender.send(watcher.into_proxy().unwrap()).await.unwrap();
                }
                reboot::RebootMethodsWatcherRegisterRequest::RegisterWithAck {
                    watcher,
                    responder,
                } => {
                    proxy_sender.send(watcher.into_proxy().unwrap()).await.unwrap();
                    responder.send().unwrap();
                }
            }
        }
    })
    .detach();
}

pub fn serve_reboot_controller(
    mut stream: controller::MockRebootControllerRequestStream,
    proxy_receiver: Arc<Mutex<mpsc::Receiver<reboot::RebootMethodsWatcherProxy>>>,
) {
    fasync::Task::spawn(async move {
        while let Some(req) = stream.try_next().await.unwrap() {
            let proxy = proxy_receiver.lock().await.next().await.unwrap();
            match req {
                controller::MockRebootControllerRequest::TriggerReboot { responder } => {
                    match proxy.on_reboot(reboot::RebootReason::UserRequest).await {
                        Err(_) => {
                            responder.send(&mut Err(controller::RebootError::ClientError)).unwrap();
                        }
                        Ok(()) => {
                            responder.send(&mut Ok(())).unwrap();
                        }
                    }
                }
                controller::MockRebootControllerRequest::CrashRebootChannel { responder } => {
                    drop(proxy);
                    responder.send(&mut Ok(())).unwrap();
                }
            }
        }
    })
    .detach();
}
