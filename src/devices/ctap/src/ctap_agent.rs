// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {crate::ctap_hid::CtapHidDevice, std::sync::Arc};

type DeviceBinding = Arc<Option<CtapHidDevice>>;

pub struct CtapAgent {
    /// Contains the ctap device currently bound to the ctap agent. Not yet used.
    /// This field is included with the unused _name escape to keep the ctap_hid crate in scope to
    /// include it's unit test.
    _device_binding: DeviceBinding,
}

impl CtapAgent {
    pub fn new() -> Self {
        CtapAgent { _device_binding: Arc::new(None) }
    }
}
