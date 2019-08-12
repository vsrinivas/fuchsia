// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use failure::{Error, ResultExt};

use fidl_fuchsia_hardware_backlight::State as BacklightState;
use fidl_fuchsia_hardware_backlight::{
    DeviceMarker as BacklightMarker, DeviceProxy as BacklightProxy,
};
use fuchsia_syslog::fx_log_info;

pub fn open_backlight() -> Result<BacklightProxy, Error> {
    fx_log_info!("Opening backlight");
    let (proxy, server) = fidl::endpoints::create_proxy::<BacklightMarker>()
        .context("Failed to create backlight proxy")?;
    // TODO(kpt): Don't hardcode this path b/138666351
    fdio::service_connect("/dev/class/backlight/000", server.into_channel())
        .context("Failed to connect built-in service")?;
    Ok(proxy)
}

pub async fn get_brightness(backlight: &BacklightProxy) -> Result<u8, Error> {
    let backlight_info = backlight.get_state().await?;
    Ok(backlight_info.brightness)
}

pub fn set_brightness(backlight: &BacklightProxy, nits: u16) -> Result<(), Error> {
    backlight.set_state(&mut BacklightState {
        on: nits != 0,
        brightness: nits as u8,
    })?;
    Ok(())
}
