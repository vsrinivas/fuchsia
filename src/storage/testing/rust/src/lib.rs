// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {anyhow::Error, fidl_fuchsia_device::ControllerProxy, fuchsia_zircon as zx};

const FVM_DRIVER_PATH: &str = "fvm.so";

pub async fn bind_fvm(proxy: &ControllerProxy) -> Result<(), Error> {
    proxy.bind(FVM_DRIVER_PATH).await?.map_err(|x| zx::Status::from_raw(x))?;
    Ok(())
}

pub async fn rebind_fvm(proxy: &ControllerProxy) -> Result<(), Error> {
    proxy.rebind(FVM_DRIVER_PATH).await?.map_err(|x| zx::Status::from_raw(x))?;
    Ok(())
}
