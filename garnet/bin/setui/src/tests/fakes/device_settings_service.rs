// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::tests::fakes::base::Service;
use anyhow::{format_err, Error};
use fidl::endpoints::{ServerEnd, ServiceMarker};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;

/// An implementation of device settings services that records stored integers invoked
/// on it.
pub struct DeviceSettingsService {
    recorded_integers: Arc<RwLock<HashMap<String, i64>>>,
}

impl DeviceSettingsService {
    pub fn new() -> Self {
        Self { recorded_integers: Arc::new(RwLock::new(HashMap::new())) }
    }

    pub fn get_integer(&self, key: String) -> Option<i64> {
        match self.recorded_integers.read().get(&key) {
            None => None,
            Some(&val) => Some(val),
        }
    }
}

impl Service for DeviceSettingsService {
    fn can_handle_service(&self, service_name: &str) -> bool {
        return service_name == fidl_fuchsia_devicesettings::DeviceSettingsManagerMarker::NAME;
    }

    fn process_stream(&self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        if !self.can_handle_service(service_name) {
            return Err(format_err!("unsupported"));
        }

        let mut manager_stream =
            ServerEnd::<fidl_fuchsia_devicesettings::DeviceSettingsManagerMarker>::new(channel)
                .into_stream()?;

        let recorded_integers_clone = self.recorded_integers.clone();

        fasync::spawn(async move {
            while let Some(req) = manager_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    fidl_fuchsia_devicesettings::DeviceSettingsManagerRequest::SetInteger {
                        key,
                        val,
                        responder,
                    } => {
                        recorded_integers_clone.write().insert(key, val);
                        responder.send(true).unwrap();
                    }
                    _ => {}
                }
            }
        });

        Ok(())
    }
}
