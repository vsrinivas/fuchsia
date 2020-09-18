// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::tests::fakes::base::Service;
use anyhow::{format_err, Error};
use fidl::endpoints::{ServerEnd, ServiceMarker};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::lock::Mutex;
use futures::TryStreamExt;
use std::sync::Arc;

#[derive(Clone)]
pub struct BrightnessService {
    manual_brightness: Arc<Mutex<Option<f32>>>,
    auto_brightness: Arc<Mutex<Option<bool>>>,
}

impl BrightnessService {
    pub fn create() -> Self {
        BrightnessService {
            manual_brightness: Arc::new(Mutex::new(None)),
            auto_brightness: Arc::new(Mutex::new(None)),
        }
    }

    pub fn get_manual_brightness(&self) -> Arc<Mutex<Option<f32>>> {
        self.manual_brightness.clone()
    }

    pub fn get_auto_brightness(&self) -> Arc<Mutex<Option<bool>>> {
        self.auto_brightness.clone()
    }
}

impl Service for BrightnessService {
    fn can_handle_service(&self, service_name: &str) -> bool {
        service_name == fidl_fuchsia_ui_brightness::ControlMarker::NAME
    }

    fn process_stream(&mut self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        if !self.can_handle_service(service_name) {
            return Err(format_err!("unsupported"));
        }

        let mut manager_stream =
            ServerEnd::<fidl_fuchsia_ui_brightness::ControlMarker>::new(channel).into_stream()?;

        let auto_brightness_handle = self.auto_brightness.clone();
        let brightness_handle = self.manual_brightness.clone();

        fasync::Task::spawn(async move {
            while let Some(req) = manager_stream.try_next().await.unwrap() {
                match req {
                    fidl_fuchsia_ui_brightness::ControlRequest::WatchCurrentBrightness {
                        responder,
                    } => {
                        responder
                            .send(brightness_handle.lock().await.expect("brightness not yet set"))
                            .unwrap();
                    }
                    fidl_fuchsia_ui_brightness::ControlRequest::SetAutoBrightness {
                        control_handle: _,
                    } => {
                        *auto_brightness_handle.lock().await = Some(true);
                    }
                    fidl_fuchsia_ui_brightness::ControlRequest::SetManualBrightness {
                        value,
                        control_handle: _,
                    } => {
                        *brightness_handle.lock().await = Some(value);
                    }
                    fidl_fuchsia_ui_brightness::ControlRequest::WatchAutoBrightness {
                        responder,
                    } => {
                        responder
                            .send(auto_brightness_handle.lock().await.unwrap_or(false))
                            .unwrap();
                    }
                    _ => {}
                }
            }
        })
        .detach();

        Ok(())
    }
}
