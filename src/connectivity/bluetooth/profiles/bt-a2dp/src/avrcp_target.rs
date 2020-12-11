// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_bluetooth_component::{LifecycleMarker, LifecycleState},
    fuchsia_component::{client, fuchsia_single_component_package_url},
};

const AVRCP_TARGET_URL: &str = fuchsia_single_component_package_url!("bt-avrcp-target");

/// Launch the AVRCP target component.  Returns the App representing the component if it is
/// running, or an error if the component could not be launched.
pub async fn launch() -> Result<client::App, Error> {
    let launcher = client::launcher()?;

    let child = client::launch(&launcher, AVRCP_TARGET_URL.to_string(), None)?;
    let lifecycle = child
        .connect_to_service::<LifecycleMarker>()
        .expect("failed to connect to component lifecycle protocol");
    loop {
        match lifecycle.get_state().await? {
            LifecycleState::Initializing => continue,
            LifecycleState::Ready => break,
        }
    }
    Ok(child)
}
