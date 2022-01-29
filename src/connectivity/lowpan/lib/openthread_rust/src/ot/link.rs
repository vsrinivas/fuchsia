// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;
use std::time::Duration;

/// Methods from the [OpenThread "Link" Module][1].
///
/// [1]: https://openthread.io/reference/group/api-link-link
pub trait Link {
    /// Functional equivalent of [`otsys::otLinkGetChannel`](crate::otsys::otLinkGetChannel).
    fn get_channel(&self) -> ChannelIndex;

    /// Functional equivalent of [`otsys::otLinkSetChannel`](crate::otsys::otLinkSetChannel).
    fn set_channel(&self, index: ChannelIndex) -> Result;

    /// Functional equivalent of
    /// [`otsys::otLinkGetExtendedAddress`](crate::otsys::otLinkGetExtendedAddress).
    fn get_extended_address(&self) -> &ExtAddress;

    /// Functional equivalent of
    /// [`otsys::otLinkGetFactoryAssignedIeeeEui64`](crate::otsys::otLinkGetFactoryAssignedIeeeEui64).
    fn get_factory_assigned_ieee_eui_64(&self) -> ExtAddress;

    /// Functional equivalent of [`otsys::otLinkGetPanId`](crate::otsys::otLinkGetPanId).
    fn get_pan_id(&self) -> PanId;

    /// Functional equivalent of [`otsys::otLinkSetPanId`](crate::otsys::otLinkSetPanId).
    fn set_pan_id(&self, pan_id: PanId) -> Result;

    /// Functional equivalent of [`otsys::otLinkGetShortAddress`](crate::otsys::otLinkGetShortAddress).
    fn get_short_address(&self) -> ShortAddress;

    /// Functional equivalent of [`otsys::otLinkIsEnabled`](crate::otsys::otLinkIsEnabled).
    fn link_is_enabled(&self) -> bool;

    /// Functional equivalent of [`otsys::otLinkSetEnabled`](crate::otsys::otLinkSetEnabled).
    fn link_set_enabled(&self, enabled: bool) -> Result;

    /// Functional equivalent of [`otsys::otLinkIsPromiscuous`](crate::otsys::otLinkIsPromiscuous).
    fn link_is_promiscuous(&self) -> bool;

    /// Functional equivalent of
    /// [`otsys::otLinkSetPromiscuous`](crate::otsys::otLinkSetPromiscuous).
    fn link_set_promiscuous(&self, promiscuous: bool) -> Result;

    /// Functional equivalent of
    /// [`otsys::otLinkIsEnergyScanInProgress`](crate::otsys::otLinkIsEnergyScanInProgress).
    fn is_energy_scan_in_progress(&self) -> bool;

    /// Functional equivalent of
    /// [`otsys::otLinkIsActiveScanInProgress`](crate::otsys::otLinkIsActiveScanInProgress).
    fn is_active_scan_in_progress(&self) -> bool;

    /// Functional equivalent of
    /// [`otsys::otLinkGetSupportedChannelMask`](crate::otsys::otLinkGetSupportedChannelMask).
    fn get_supported_channel_mask(&self) -> ot::ChannelMask;

    /// Starts an active scan. Functional equivalent of
    /// [`otsys::otLinkActiveScan`](crate::otsys::otLinkActiveScan).
    ///
    /// The closure will ultimately be executed via
    /// [`ot::Tasklets::process`](crate::ot::Tasklets::process).
    fn start_active_scan<'a, F>(&self, channels: ChannelMask, dwell: Duration, f: F) -> Result
    where
        F: FnMut(Option<&ActiveScanResult>) + 'a;

    /// Starts an energy scan. Functional equivalent of
    /// [`otsys::otLinkEnergyScan`](crate::otsys::otLinkEnergyScan).
    ///
    /// The closure will ultimately be executed via
    /// [`ot::Tasklets::process`](crate::ot::Tasklets::process).
    fn start_energy_scan<'a, F>(&self, channels: ChannelMask, dwell: Duration, f: F) -> Result
    where
        F: FnMut(Option<&EnergyScanResult>) + 'a;
}

impl<T: Link + Boxable> Link for ot::Box<T> {
    fn get_channel(&self) -> ChannelIndex {
        self.as_ref().get_channel()
    }

    fn set_channel(&self, index: ChannelIndex) -> Result {
        self.as_ref().set_channel(index)
    }

    fn get_extended_address(&self) -> &ExtAddress {
        self.as_ref().get_extended_address()
    }
    fn get_factory_assigned_ieee_eui_64(&self) -> ExtAddress {
        self.as_ref().get_factory_assigned_ieee_eui_64()
    }

    fn get_pan_id(&self) -> PanId {
        self.as_ref().get_pan_id()
    }
    fn set_pan_id(&self, pan_id: PanId) -> Result<()> {
        self.as_ref().set_pan_id(pan_id)
    }

    fn get_short_address(&self) -> ShortAddress {
        self.as_ref().get_short_address()
    }

    fn link_is_enabled(&self) -> bool {
        self.as_ref().link_is_enabled()
    }
    fn link_set_enabled(&self, enabled: bool) -> Result {
        self.as_ref().link_set_enabled(enabled)
    }

    fn link_is_promiscuous(&self) -> bool {
        self.as_ref().link_is_promiscuous()
    }
    fn link_set_promiscuous(&self, promiscuous: bool) -> Result {
        self.as_ref().link_set_promiscuous(promiscuous)
    }

    fn is_energy_scan_in_progress(&self) -> bool {
        self.as_ref().is_energy_scan_in_progress()
    }
    fn is_active_scan_in_progress(&self) -> bool {
        self.as_ref().is_active_scan_in_progress()
    }

    fn get_supported_channel_mask(&self) -> ChannelMask {
        self.as_ref().get_supported_channel_mask()
    }

    fn start_active_scan<'a, F>(&self, channels: ChannelMask, dwell: Duration, f: F) -> Result
    where
        F: FnMut(Option<&ActiveScanResult>) + 'a,
    {
        self.as_ref().start_active_scan(channels, dwell, f)
    }

    fn start_energy_scan<'a, F>(&self, channels: ChannelMask, dwell: Duration, f: F) -> Result
    where
        F: FnMut(Option<&EnergyScanResult>) + 'a,
    {
        self.as_ref().start_energy_scan(channels, dwell, f)
    }
}

impl Link for Instance {
    fn get_channel(&self) -> ChannelIndex {
        unsafe { otLinkGetChannel(self.as_ot_ptr()) }
    }

    fn set_channel(&self, index: ChannelIndex) -> Result {
        Error::from(unsafe { otLinkSetChannel(self.as_ot_ptr(), index) }).into()
    }

    fn get_extended_address(&self) -> &ExtAddress {
        unsafe {
            let ext_addr = otLinkGetExtendedAddress(self.as_ot_ptr());
            ExtAddress::ref_from_ot_ptr(ext_addr).unwrap()
        }
    }

    fn get_factory_assigned_ieee_eui_64(&self) -> ExtAddress {
        unsafe {
            let mut ext_addr = ExtAddress::default();
            otLinkGetFactoryAssignedIeeeEui64(self.as_ot_ptr(), ext_addr.as_ot_mut_ptr());
            ext_addr
        }
    }

    fn get_pan_id(&self) -> PanId {
        unsafe { otLinkGetPanId(self.as_ot_ptr()) }
    }

    fn set_pan_id(&self, pan_id: PanId) -> Result {
        Error::from(unsafe { otLinkSetPanId(self.as_ot_ptr(), pan_id) }).into()
    }

    fn get_short_address(&self) -> ShortAddress {
        unsafe { otLinkGetShortAddress(self.as_ot_ptr()) }
    }

    fn link_is_enabled(&self) -> bool {
        unsafe { otLinkIsEnabled(self.as_ot_ptr()) }
    }

    fn link_set_enabled(&self, enabled: bool) -> Result {
        Error::from(unsafe { otLinkSetEnabled(self.as_ot_ptr(), enabled) }).into()
    }

    fn link_is_promiscuous(&self) -> bool {
        unsafe { otLinkIsPromiscuous(self.as_ot_ptr()) }
    }

    fn link_set_promiscuous(&self, promiscuous: bool) -> Result {
        Error::from(unsafe { otLinkSetPromiscuous(self.as_ot_ptr(), promiscuous) }).into()
    }

    fn is_energy_scan_in_progress(&self) -> bool {
        unsafe { otLinkIsEnergyScanInProgress(self.as_ot_ptr()) }
    }

    fn is_active_scan_in_progress(&self) -> bool {
        unsafe { otLinkIsActiveScanInProgress(self.as_ot_ptr()) }
    }

    fn get_supported_channel_mask(&self) -> ChannelMask {
        let mask_u32 = unsafe { otLinkGetSupportedChannelMask(self.as_ot_ptr()) };
        mask_u32.into()
    }

    fn start_active_scan<'a, F>(&self, channels: ChannelMask, dwell: Duration, f: F) -> Result
    where
        F: FnMut(Option<&ActiveScanResult>) + 'a,
    {
        unsafe extern "C" fn _ot_handle_active_scan_result<
            'a,
            F: FnMut(Option<&ActiveScanResult>) + 'a,
        >(
            result: *mut otActiveScanResult,
            context: *mut ::std::os::raw::c_void,
        ) {
            trace!("_ot_handle_active_scan_result: {:?}", result);

            // Convert the `*otActiveScanResult` into an `Option<&ot::ActiveScanResult>`.
            let result = ActiveScanResult::ref_from_ot_ptr(result);

            // Reconstitute a reference to our closure.
            let sender = &mut *(context as *mut F);

            sender(result);
        }

        let (fn_ptr, fn_box, cb): (_, _, otHandleActiveScanResult) = {
            let mut x = Box::new(f);

            (
                x.as_mut() as *mut F as *mut ::std::os::raw::c_void,
                Some(x as Box<dyn FnMut(Option<&ActiveScanResult>) + 'a>),
                Some(_ot_handle_active_scan_result::<F>),
            )
        };

        unsafe {
            Error::from(otLinkActiveScan(
                self.as_ot_ptr(),
                channels.into(),
                dwell.as_millis().try_into().unwrap(),
                cb,
                fn_ptr,
            ))
            .into_result()?;

            // Make sure our object eventually gets cleaned up.
            // Here we must also transmute our closure to have a 'static lifetime.
            // We need to do this because the borrow checker cannot infer the
            // proper lifetime for the singleton instance backing, but
            // this is guaranteed by the API.
            self.borrow_backing().active_scan_fn.set(std::mem::transmute::<
                Option<Box<dyn FnMut(Option<&ActiveScanResult>) + 'a>>,
                Option<Box<dyn FnMut(Option<&ActiveScanResult>) + 'static>>,
            >(fn_box));
        }

        Ok(())
    }

    fn start_energy_scan<'a, F>(&self, channels: ChannelMask, dwell: Duration, f: F) -> Result
    where
        F: FnMut(Option<&EnergyScanResult>) + 'a,
    {
        unsafe extern "C" fn _ot_handle_energy_scan_result<
            'a,
            F: FnMut(Option<&EnergyScanResult>) + 'a,
        >(
            result: *mut otEnergyScanResult,
            context: *mut ::std::os::raw::c_void,
        ) {
            trace!("_ot_handle_energy_scan_result: {:?}", result);

            // Convert the `*otEnergyScanResult` into an `Option<&ot::EnergyScanResult>`.
            let result = EnergyScanResult::ref_from_ot_ptr(result);

            // Reconstitute a reference to our closure.
            let sender = &mut *(context as *mut F);

            sender(result);
        }

        let (fn_ptr, fn_box, cb): (_, _, otHandleEnergyScanResult) = {
            let mut x = Box::new(f);

            (
                x.as_mut() as *mut F as *mut ::std::os::raw::c_void,
                Some(x as Box<dyn FnMut(Option<&EnergyScanResult>) + 'a>),
                Some(_ot_handle_energy_scan_result::<F>),
            )
        };

        unsafe {
            Error::from(otLinkEnergyScan(
                self.as_ot_ptr(),
                channels.into(),
                dwell.as_millis().try_into().unwrap(),
                cb,
                fn_ptr,
            ))
            .into_result()?;

            // Make sure our object eventually gets cleaned up.
            // Here we must also transmute our closure to have a 'static lifetime.
            // We need to do this because the borrow checker cannot infer the
            // proper lifetime for the singleton instance backing, but
            // this is guaranteed by the API.
            self.borrow_backing().energy_scan_fn.set(std::mem::transmute::<
                Option<Box<dyn FnMut(Option<&EnergyScanResult>) + 'a>>,
                Option<Box<dyn FnMut(Option<&EnergyScanResult>) + 'static>>,
            >(fn_box));
        }

        Ok(())
    }
}
