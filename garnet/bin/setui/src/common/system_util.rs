// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::registry::service_context::ServiceContext;
use parking_lot::RwLock;
use std::sync::Arc;

pub async fn reboot(service_context_handle: Arc<RwLock<ServiceContext>>) {
  let device_admin = service_context_handle
    .read()
    .connect::<fidl_fuchsia_device_manager::AdministratorMarker>()
    .expect("connected to device manager");

  device_admin.suspend(fidl_fuchsia_device_manager::SUSPEND_FLAG_REBOOT).await.ok();
}
