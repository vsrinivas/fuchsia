// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Methods from the [OpenThread "Radio" Module][1].
///
/// [1]: https://openthread.io/reference/group/radio-operation
pub trait Radio {
    /// Functional equivalent of
    /// [`otsys::otPlatRadioGetCoexMetrics`](crate::otsys::otPlatRadioGetCoexMetrics).
    fn get_coex_metrics(&self) -> Result<RadioCoexMetrics>;

    /// Functional equivalent of
    /// [`otsys::otPlatRadioGetRssi`](crate::otsys::otPlatRadioGetRssi).
    fn get_rssi(&self) -> Decibels;

    /// Functional equivalent of
    /// [`otsys::otPlatRadioGetRegion`](crate::otsys::otPlatRadioGetRegion).
    fn get_region(&self) -> Result<RadioRegion>;

    /// Functional equivalent of
    /// [`otsys::otPlatRadioSetRegion`](crate::otsys::otPlatRadioSetRegion).
    fn set_region(&self, region: RadioRegion) -> Result;

    /// Functional equivalent of
    /// [`otsys::otPlatRadioGetTransmitPower`](crate::otsys::otPlatRadioGetTransmitPower).
    fn get_transmit_power(&self) -> Result<Decibels>;

    /// Functional equivalent of
    /// [`otsys::otPlatRadioGetVersionString`](crate::otsys::otPlatRadioGetVersionString).
    fn radio_get_version_string(&self) -> &str;
}

impl<T: Radio + Boxable> Radio for ot::Box<T> {
    fn get_coex_metrics(&self) -> Result<RadioCoexMetrics> {
        self.as_ref().get_coex_metrics()
    }

    fn get_rssi(&self) -> Decibels {
        self.as_ref().get_rssi()
    }

    fn get_region(&self) -> Result<RadioRegion> {
        self.as_ref().get_region()
    }

    fn set_region(&self, region: RadioRegion) -> Result {
        self.as_ref().set_region(region)
    }

    fn get_transmit_power(&self) -> Result<Decibels> {
        self.as_ref().get_transmit_power()
    }

    fn radio_get_version_string(&self) -> &str {
        self.as_ref().radio_get_version_string()
    }
}

impl Radio for Instance {
    fn get_coex_metrics(&self) -> Result<RadioCoexMetrics> {
        let mut ret = RadioCoexMetrics::default();
        Error::from(unsafe { otPlatRadioGetCoexMetrics(self.as_ot_ptr(), ret.as_ot_mut_ptr()) })
            .into_result()?;
        Ok(ret)
    }

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

    fn get_transmit_power(&self) -> Result<Decibels> {
        let mut ret = DECIBELS_UNSPECIFIED;
        Error::from(unsafe {
            otPlatRadioGetTransmitPower(self.as_ot_ptr(), &mut ret as *mut Decibels)
        })
        .into_result()?;
        Ok(ret)
    }

    fn radio_get_version_string(&self) -> &str {
        unsafe {
            // SAFETY: `otPlatRadioGetVersionString` guarantees to return a C-String that will not
            //         change.
            CStr::from_ptr(otPlatRadioGetVersionString(self.as_ot_ptr()))
                .to_str()
                .expect("OpenThread version string was bad UTF8")
        }
    }
}
