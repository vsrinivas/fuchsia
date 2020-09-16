// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::fidl_hanging_get_responder,
    crate::fidl_process,
    crate::fidl_processor::RequestContext,
    crate::switchboard::base::*,
    crate::switchboard::hanging_get_handler::Sender,
    fidl_fuchsia_settings::{DeviceMarker, DeviceRequest, DeviceSettings, DeviceWatchResponder},
    futures::future::LocalBoxFuture,
    futures::FutureExt,
};

fidl_hanging_get_responder!(DeviceMarker, DeviceSettings, DeviceWatchResponder);

impl From<SettingResponse> for DeviceSettings {
    fn from(response: SettingResponse) -> Self {
        if let SettingResponse::Device(info) = response {
            let mut device_settings = fidl_fuchsia_settings::DeviceSettings::empty();
            device_settings.build_tag = Some(info.build_tag);
            device_settings
        } else {
            panic!("incorrect value sent to device handler");
        }
    }
}

fidl_process!(Device, SettingType::Device, process_request);

async fn process_request(
    context: RequestContext<DeviceSettings, DeviceWatchResponder>,
    req: DeviceRequest,
) -> Result<Option<DeviceRequest>, anyhow::Error> {
    // Support future expansion of FIDL
    #[allow(unreachable_patterns)]
    match req {
        DeviceRequest::Watch { responder } => {
            context.watch(responder, true).await;
        }
        _ => {
            return Ok(Some(req));
        }
    }

    return Ok(None);
}
