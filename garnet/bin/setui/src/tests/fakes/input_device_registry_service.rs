// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::fidl_clone::FIDLClone;
use crate::tests::fakes::base::Service;
use anyhow::{format_err, Error};
use fidl::endpoints::{ServerEnd, ServiceMarker};
use fidl_fuchsia_ui_input::MediaButtonsEvent;
use fidl_fuchsia_ui_policy::MediaButtonsListenerProxy;
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use parking_lot::RwLock;
use std::sync::Arc;

pub struct InputDeviceRegistryService {
    listeners: Arc<RwLock<Vec<MediaButtonsListenerProxy>>>,
    last_sent_event: Arc<RwLock<Option<MediaButtonsEvent>>>,
    fail: bool,
}

impl InputDeviceRegistryService {
    pub fn new() -> Self {
        Self {
            listeners: Arc::new(RwLock::new(Vec::new())),
            last_sent_event: Arc::new(RwLock::new(None)),
            fail: false,
        }
    }

    pub fn set_fail(&mut self, fail: bool) {
        self.fail = fail;
    }

    pub fn send_media_button_event(&self, event: MediaButtonsEvent) {
        *self.last_sent_event.write() = Some(event.clone());
        for listener in self.listeners.read().iter() {
            listener.on_media_buttons_event(event.clone()).ok();
        }
    }
}

impl Service for InputDeviceRegistryService {
    fn can_handle_service(&self, service_name: &str) -> bool {
        return service_name == fidl_fuchsia_ui_policy::DeviceListenerRegistryMarker::NAME;
    }

    fn process_stream(&mut self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        if !self.can_handle_service(service_name) {
            return Err(format_err!("can't handle service"));
        }

        let mut manager_stream =
            ServerEnd::<fidl_fuchsia_ui_policy::DeviceListenerRegistryMarker>::new(channel)
                .into_stream()?;

        let listeners_handle = self.listeners.clone();
        let last_event = self.last_sent_event.clone();

        if self.fail {
            return Err(format_err!("exiting early"));
        }

        fasync::Task::spawn(async move {
            while let Some(req) = manager_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    fidl_fuchsia_ui_policy::DeviceListenerRegistryRequest::RegisterMediaButtonsListener {
                        listener,
                        control_handle: _,
                    } => {
                        if let Ok(proxy) = listener.into_proxy() {
                            if let Some(event) = &*last_event.read() {
                                proxy.on_media_buttons_event(event.clone()).ok();
                            }
                            listeners_handle.write().push(proxy);
                        }
                    }
                    _ => {}
                }
            }
        }).detach();

        Ok(())
    }
}
