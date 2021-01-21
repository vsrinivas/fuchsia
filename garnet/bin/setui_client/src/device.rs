// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::utils::{formatted_watch_to_stream, StringTryStream},
    fidl_fuchsia_settings::{DeviceProxy, DeviceSettings},
};

pub fn command(proxy: DeviceProxy) -> StringTryStream {
    formatted_watch_to_stream(proxy, |p| p.watch(), |d| describe_device(&d))
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
