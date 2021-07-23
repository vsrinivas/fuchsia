// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::tests::fakes::base::Service;

use anyhow::{format_err, Error};
use fidl::endpoints::{ProtocolMarker, ServerEnd};
use fidl_fuchsia_camera3::{
    DeviceRequest, DeviceWatcherMarker, DeviceWatcherRequest, WatchDevicesEvent,
};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;

pub(crate) struct Camera3Service {
    camera_sw_muted: Arc<AtomicBool>,
}

impl Camera3Service {
    pub(crate) fn new() -> Self {
        Self { camera_sw_muted: Arc::new(AtomicBool::new(false)) }
    }

    pub(crate) fn camera_sw_muted(&self) -> bool {
        (*self.camera_sw_muted).load(Ordering::Relaxed)
    }

    pub(crate) fn set_camera_sw_muted(&self, muted: bool) {
        (*self.camera_sw_muted).swap(muted, Ordering::Relaxed);
    }
}

impl Service for Camera3Service {
    fn can_handle_service(&self, service_name: &str) -> bool {
        service_name == DeviceWatcherMarker::NAME
    }

    fn process_stream(&mut self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        if !self.can_handle_service(service_name) {
            return Err(format_err!("can't handle service"));
        }

        let mut device_watcher_stream =
            ServerEnd::<DeviceWatcherMarker>::new(channel).into_stream()?;

        let camera_sw_muted = Arc::clone(&self.camera_sw_muted);
        fasync::Task::spawn(async move {
            while let Some(req) = device_watcher_stream.try_next().await.unwrap() {
                // Support future expansion of FIDL.
                #[allow(unreachable_patterns)]
                match req {
                    DeviceWatcherRequest::WatchDevices { responder } => responder
                        .send(&mut vec![&mut WatchDevicesEvent::Added(1)].into_iter())
                        .expect("Failed to send devices response"),
                    DeviceWatcherRequest::ConnectToDevice {
                        id: _,
                        request, // ServerEnd<DeviceMarker>
                        control_handle: _,
                    } => {
                        let mut stream = request.into_stream().unwrap();
                        let camera_sw_muted = Arc::clone(&camera_sw_muted);
                        fasync::Task::spawn(async move {
                            while let Some(req) = stream.try_next().await.unwrap() {
                                // Support future expansion of FIDL.
                                match req {
                                    DeviceRequest::SetSoftwareMuteState { muted, responder } => {
                                        camera_sw_muted.swap(muted, Ordering::Relaxed);
                                        let _ = responder.send();
                                    }
                                    DeviceRequest::WatchMuteState { responder } => {
                                        let _ = responder
                                            .send(camera_sw_muted.load(Ordering::Relaxed), false);
                                    }
                                    _ => {}
                                }
                            }
                        })
                        .detach();
                    }
                    _ => {}
                }
            }
        })
        .detach();

        Ok(())
    }
}
