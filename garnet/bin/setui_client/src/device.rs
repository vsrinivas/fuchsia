// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_settings::{DeviceProxy, DeviceSettings},
    futures::{TryFutureExt, TryStream},
};

pub fn command(proxy: DeviceProxy) -> impl TryStream<Ok = String, Error = Error> {
    futures::stream::try_unfold(proxy, |proxy| {
        proxy
            .watch()
            .map_ok(move |settings| Some((describe_device(&settings), proxy)))
            .map_err(Into::into)
    })
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
