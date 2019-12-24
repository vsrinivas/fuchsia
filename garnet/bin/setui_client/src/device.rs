// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_settings::{DeviceProxy, DeviceSettings},
};

pub async fn command(proxy: DeviceProxy) -> Result<String, Error> {
    let mut output = String::new();

    let settings = proxy.watch().await?;
    let settings_string = describe_device(&settings);
    output.push_str(&settings_string);

    Ok(output)
}

fn describe_device(device_settings: &DeviceSettings) -> String {
    let mut output = String::new();

    output.push_str("DeviceSettings {\n ");

    if let Some(build_tag) = &device_settings.build_tag {
        output.push_str(&format!("build_tag: {}\n", build_tag));
    }

    output.push_str("}");

    output
}
