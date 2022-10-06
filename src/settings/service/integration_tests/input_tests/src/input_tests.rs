// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::common::{InputTest, Mocks};
use anyhow::Error;
use async_trait::async_trait;
use fidl_fuchsia_camera3::{
    DeviceRequest, DeviceWatcherRequest, DeviceWatcherRequestStream, WatchDevicesEvent,
};
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_component_test::LocalComponentHandles;
use futures::{StreamExt, TryStreamExt};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
mod common;

#[async_trait]
impl Mocks for InputTest {
    // Mock the camera dependency and verify the settings service interacts with the camera
    // dependency by checking the cam_muted has been updated to default value false through the
    // SetSoftwareMuteState request.
    async fn device_watcher_impl(
        handles: LocalComponentHandles,
        cam_muted: Arc<AtomicBool>,
    ) -> Result<(), Error> {
        let mut fs = ServiceFs::new();
        let _: &mut ServiceFsDir<'_, _> =
            fs.dir("svc").add_fidl_service(move |mut stream: DeviceWatcherRequestStream| {
                let cam_muted = Arc::clone(&cam_muted);
                fasync::Task::spawn(async move {
                    while let Ok(Some(req)) = stream.try_next().await {
                        // Support future expansion of FIDL.
                        #[allow(unreachable_patterns)]
                        match req {
                            DeviceWatcherRequest::WatchDevices { responder } => {
                                let mut camera_device = WatchDevicesEvent::Added(1);
                                let camera_devices = vec![&mut camera_device];

                                let mut devices = camera_devices.into_iter();
                                responder
                                    .send(&mut devices)
                                    .expect("Failed to send devices response");
                            }
                            DeviceWatcherRequest::ConnectToDevice {
                                id: _,
                                request,
                                control_handle: _,
                            } => {
                                let mut stream = request.into_stream().unwrap();
                                let cam_muted = Arc::clone(&cam_muted);
                                fasync::Task::spawn(async move {
                                    while let Some(req) = stream.try_next().await.unwrap() {
                                        match req {
                                            DeviceRequest::SetSoftwareMuteState {
                                                muted,
                                                responder,
                                            } => {
                                                (*cam_muted).store(muted, Ordering::Relaxed);
                                                let _ = responder.send();
                                            }
                                            _ => {}
                                        }
                                    }
                                })
                                .detach();
                            }
                        }
                    }
                })
                .detach();
            });
        let _: &mut ServiceFs<_> = fs.serve_connection(handles.outgoing_dir).unwrap();
        fs.collect::<()>().await;
        Ok(())
    }
}

// Verify settings service interacts with the camera service on boot.
#[fuchsia::test]
async fn test_inputmarker() {
    let camera_sw_muted = Arc::new(AtomicBool::new(true));
    let instance =
        InputTest::create_realm(Arc::clone(&camera_sw_muted)).await.expect("setting up test realm");
    let proxy = InputTest::connect_to_inputmarker(&instance);
    // Make a watch call.
    let _ = proxy.watch().await.expect("watch completed");
    // Assert the camera_sw_muted value changes to default false.
    assert!(!camera_sw_muted.load(Ordering::Relaxed));

    let _ = instance.destroy().await;
}
