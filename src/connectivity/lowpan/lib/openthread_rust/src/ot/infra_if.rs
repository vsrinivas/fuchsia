// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Docs
pub trait InfraInterface {
    /// Docs
    fn platform_infra_if_on_state_changed(&self, id: u32, is_running: bool);
}

impl<T: InfraInterface + ot::Boxable> InfraInterface for ot::Box<T> {
    fn platform_infra_if_on_state_changed(&self, id: u32, is_running: bool) {
        self.as_ref().platform_infra_if_on_state_changed(id, is_running);
    }
}

impl InfraInterface for Instance {
    fn platform_infra_if_on_state_changed(&self, id: u32, is_running: bool) {
        unsafe {
            otPlatInfraIfStateChanged(self.as_ot_ptr(), id, is_running);
        }
    }
}
