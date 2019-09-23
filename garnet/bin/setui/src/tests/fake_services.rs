// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::registry::service_context::GenerateService;
use failure::format_err;
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

pub struct FakeDeviceAdmin {
  recorded_actions: Arc<RwLock<Vec<Action>>>,
}

impl FakeDeviceAdmin {
  pub fn new() -> Self {
    Self { recorded_actions: Arc::new(RwLock::new(Vec::new())) }
  }

  pub fn get_service(&self) -> GenerateService {
    let recorded_actions_clone = self.recorded_actions.clone();

    return Box::new(move |service_name: &str, channel: zx::Channel| {
      if service_name != fidl_fuchsia_device_manager::AdministratorMarker::NAME {
        return Err(format_err!("unsupported!"));
      }

      let mut manager_stream =
        ServerEnd::<fidl_fuchsia_device_manager::AdministratorMarker>::new(channel)
          .into_stream()?;

      let recorded_actions_clone = recorded_actions_clone.clone();
      fasync::spawn(async move {
        while let Some(req) = manager_stream.try_next().await.unwrap() {
          #[allow(unreachable_patterns)]
          match req {
            fidl_fuchsia_device_manager::AdministratorRequest::Suspend { flags: _, responder } => {
              recorded_actions_clone.write().push(Action::Reboot);
              responder.send(0).unwrap();
            }
            _ => {}
          }
        }
      });

      Ok(())
    });
  }

  pub fn get_actions(&self) -> Arc<RwLock<Vec<Action>>> {
    return self.recorded_actions.clone();
  }
}
