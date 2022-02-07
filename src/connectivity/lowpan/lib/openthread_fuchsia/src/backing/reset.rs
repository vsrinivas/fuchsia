// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::*;

impl PlatformBacking {
    fn on_plat_reset(&self, instance: Option<&ot::Instance>) {
        #[no_mangle]
        unsafe extern "C" fn otPlatReset(instance: *mut otsys::otInstance) {
            PlatformBacking::on_plat_reset(
                // SAFETY: Must only be called from OpenThread thread,
                PlatformBacking::as_ref(),
                // SAFETY: `instance` must be a pointer to a valid `otInstance`,
                //         which is guaranteed by the caller.
                ot::Instance::ref_from_ot_ptr(instance),
            )
        }

        if let Some(instance) = instance {
            instance.thread_set_enabled(false).unwrap();
            instance.ip6_set_enabled(false).unwrap();
        }

        info!("on_plat_reset for {:?}", instance);
    }
}
