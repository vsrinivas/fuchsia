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
pub struct RecoveryPolicy {
    is_local_reset_allowed: Arc<Mutex<Option<bool>>>,
}

impl RecoveryPolicy {
    pub fn create() -> Self {
        Self { is_local_reset_allowed: Arc::new(Mutex::new(None)) }
    }

    pub fn is_local_reset_allowed(&self) -> Arc<Mutex<Option<bool>>> {
        self.is_local_reset_allowed.clone()
    }
}

impl Service for RecoveryPolicy {
    fn can_handle_service(&self, service_name: &str) -> bool {
        service_name == fidl_fuchsia_recovery_policy::DeviceMarker::NAME
    }

    fn process_stream(&mut self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        if !self.can_handle_service(service_name) {
            return Err(format_err!("unsupported"));
        }

        let mut manager_stream =
            ServerEnd::<fidl_fuchsia_recovery_policy::DeviceMarker>::new(channel).into_stream()?;

        let local_reset_allowed_handle = self.is_local_reset_allowed.clone();

        fasync::Task::spawn(async move {
            while let Some(req) = manager_stream.try_next().await.unwrap() {
                // Support future expansion of FIDL.
                #[allow(unreachable_patterns)]
                match req {
                    fidl_fuchsia_recovery_policy::DeviceRequest::SetIsLocalResetAllowed {
                        allowed,
                        control_handle: _,
                    } => {
                        *local_reset_allowed_handle.lock().await = Some(allowed);
                    }
                    _ => {}
                }
            }
        })
        .detach();

        Ok(())
    }
}
