// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::{Proxy, ServerEnd};
use fidl_fuchsia_settings::{
    LightGroup as LightGroupFidl, LightMarker, LightProxy, LightRequestStream,
};
use fidl_fuchsia_ui_brightness::{ControlMarker, ControlProxy, ControlRequestStream};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;

/// Returns whether two floats are close enough to be considered equal.
pub(crate) fn close_enough(left: f32, right: f32) -> bool {
    (left - right).abs() <= std::f32::EPSILON
}

/// Common data used for setting up tests.
pub(crate) struct SetupData {
    pub(crate) light: (LightProxy, LightRequestStream),
    pub(crate) control: (ControlProxy, ControlRequestStream),
    pub(crate) light_groups: Vec<LightGroupFidl>,
}

pub(crate) const LED1_NAME: &str = "led1";
pub(crate) const LED2_NAME: &str = "led2";

/// Helper function for setting up proxies with default data.
pub(crate) fn setup_proxies_and_data() -> SetupData {
    // Create light proxy and request stream.
    let (proxy, server) = zx::Channel::create().expect("Cannot create channel");
    let light_requests = ServerEnd::<LightMarker>::new(server)
        .into_stream()
        .expect("Cannot convert channel to Light server end");

    let channel =
        fasync::Channel::from_channel(proxy).expect("Cannot create async channel from zx channel");
    let light_proxy = LightProxy::from_channel(channel);

    // Create control proxy and request stream.
    let (proxy, server) = zx::Channel::create().expect("Cannot create channel");
    let brightness_requests = ServerEnd::<ControlMarker>::new(server)
        .into_stream()
        .expect("Cannot convert channel to Light server end");

    let channel =
        fasync::Channel::from_channel(proxy).expect("Cannot create async channel from zx channel");
    let brightness_proxy = ControlProxy::from_channel(channel);

    // Create default, disabled light groups.
    let light_groups = vec![
        LightGroupFidl { name: Some(String::from(LED1_NAME)), ..LightGroupFidl::EMPTY },
        LightGroupFidl { name: Some(String::from(LED2_NAME)), ..LightGroupFidl::EMPTY },
    ];

    SetupData {
        light: (light_proxy, light_requests),
        control: (brightness_proxy, brightness_requests),
        light_groups,
    }
}
