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
use std::sync::Arc;

#[derive(PartialEq, Debug, Eq, Hash, Clone, Copy)]
pub enum Action {
    Reboot,
}

/// An implementation of device admin services that records the actions invoked
/// on it.
pub struct DeviceAdminService {
    recorded_actions: Arc<RwLock<Vec<Action>>>,
}

impl DeviceAdminService {
    pub fn new() -> Self {
        Self { recorded_actions: Arc::new(RwLock::new(Vec::new())) }
    }

    pub fn verify_action_sequence(&self, actions: Vec<Action>) -> bool {
        let recorded_actions = self.recorded_actions.read();

        for iter in recorded_actions.iter().zip(actions.iter()) {
            let (action1, action2) = iter;
            if action1 != action2 {
                return false;
            }
        }

        return true;
    }
}

impl Service for DeviceAdminService {
    fn can_handle_service(&self, service_name: &str) -> bool {
        return service_name == fidl_fuchsia_device_manager::AdministratorMarker::NAME;
    }

    fn process_stream(&self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        if !self.can_handle_service(service_name) {
            return Err(format_err!("unsupported"));
        }

        let mut manager_stream =
            ServerEnd::<fidl_fuchsia_device_manager::AdministratorMarker>::new(channel)
                .into_stream()?;

        let recorded_actions_clone = self.recorded_actions.clone();
        fasync::spawn(async move {
            while let Some(req) = manager_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    fidl_fuchsia_device_manager::AdministratorRequest::Suspend {
                        flags: _,
                        responder,
                    } => {
                        recorded_actions_clone.write().push(Action::Reboot);
                        responder.send(0).unwrap();
                    }
                    _ => {}
                }
            }
        });

        Ok(())
    }
}
