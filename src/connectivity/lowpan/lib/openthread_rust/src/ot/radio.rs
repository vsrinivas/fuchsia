// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Methods from the [OpenThread "Radio" Module][1].
///
/// [1]: https://openthread.io/reference/group/radio-operation
pub trait Radio {
    /// Functional equivalent of
    /// [`otsys::otPlatRadioGetRssi`](crate::otsys::otPlatRadioGetRssi).
    fn get_rssi(&self) -> Decibels;

    /// Functional equivalent of
    /// [`otsys::otPlatRadioGetRegion`](crate::otsys::otPlatRadioGetRegion).
    fn get_region(&self) -> Result<RadioRegion>;

    /// Functional equivalent of
    /// [`otsys::otPlatRadioSetRegion`](crate::otsys::otPlatRadioSetRegion).
    fn set_region(&self, region: RadioRegion) -> Result;
}

impl<T: Radio + Boxable> Radio for ot::Box<T> {
    fn get_rssi(&self) -> Decibels {
        self.as_ref().get_rssi()
    }

    fn get_region(&self) -> Result<RadioRegion> {
        self.as_ref().get_region()
    }

    fn set_region(&self, region: RadioRegion) -> Result {
        self.as_ref().set_region(region)
    }
}

impl Radio for Instance {
    fn get_rssi(&self) -> Decibels {
        unsafe { otPlatRadioGetRssi(self.as_ot_ptr()) }
    }

    fn get_region(&self) -> Result<RadioRegion> {
        let mut ret = 0u16;
        Error::from(unsafe { otPlatRadioGetRegion(self.as_ot_ptr(), &mut ret as *mut u16) })
            .into_result()?;
        Ok(ret.into())
    }

    fn set_region(&self, region: RadioRegion) -> Result {
        Error::from(unsafe { otPlatRadioSetRegion(self.as_ot_ptr(), region.into()) }).into()
    }
}
