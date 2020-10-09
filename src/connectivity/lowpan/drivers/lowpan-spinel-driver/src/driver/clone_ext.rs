// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Hack to provide a `clone()` method for `fidl_fuchsia_lowpan_device::DeviceState`
/// and `fidl_fuchsia_lowpan::Identity` until FTP-057 lands.
///
/// I'm calling it a hack because ideally `fidl_fuchsia_lowpan::Identity`
/// and `fidl_fuchsia_lowpan_device::DeviceState` would implement `std::clone::Clone`
/// directly. Currently they do not, which will be fixed when FTP-057 lands.
/// But we still need the ability to clone these values. Due to hygiene rules,
/// we cannot implement `std::clone::Clone` for these types from here, so we
/// define a new trait `CloneExt` that we can implement on the types.
pub(super) trait CloneExt {
    fn clone(&self) -> Self;
}

impl CloneExt for fidl_fuchsia_lowpan_device::DeviceState {
    fn clone(&self) -> Self {
        Self { connectivity_state: self.connectivity_state.clone(), role: self.role.clone() }
    }
}

impl CloneExt for fidl_fuchsia_lowpan::Identity {
    fn clone(&self) -> Self {
        Self {
            raw_name: self.raw_name.clone(),
            xpanid: self.xpanid.clone(),
            net_type: self.net_type.clone(),
            channel: self.channel.clone(),
            panid: self.panid.clone(),
            mesh_local_prefix: self.mesh_local_prefix.clone(),
        }
    }
}
