// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Reset-related methods from the [OpenThread "Instance" Module][1].
///
/// [1]: https://openthread.io/reference/group/api-instance
pub trait Reset {
    /// Functional equivalent of [`otsys::otInstanceReset`](crate::otsys::otInstanceReset).
    fn reset(&self);

    /// Functional equivalent of
    /// [`otsys::otInstanceFactoryReset`](crate::otsys::otInstanceFactoryReset).
    fn factory_reset(&self);

    /// Functional equivalent of
    /// [`otsys::otInstanceErasePersistentInfo`](crate::otsys::otInstanceErasePersistentInfo).
    fn erase_persistent_info(&self) -> Result;
}

impl<T: Reset + Boxable> Reset for ot::Box<T> {
    fn reset(&self) {
        self.as_ref().reset()
    }
    fn factory_reset(&self) {
        self.as_ref().factory_reset()
    }
    fn erase_persistent_info(&self) -> Result {
        self.as_ref().erase_persistent_info()
    }
}

impl Reset for Instance {
    fn reset(&self) {
        unsafe { otInstanceReset(self.as_ot_ptr()) }
    }

    fn factory_reset(&self) {
        unsafe { otInstanceFactoryReset(self.as_ot_ptr()) }
    }

    fn erase_persistent_info(&self) -> Result {
        Error::from(unsafe { otInstanceErasePersistentInfo(self.as_ot_ptr()) }).into()
    }
}
