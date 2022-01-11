// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

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

    /// Functional equivalent of
    /// [`otsys::otPlatRadioGetRssi`](crate::otsys::otPlatRadioGetRssi).
    fn get_rssi(&self) -> Decibels;
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

    fn get_rssi(&self) -> Decibels {
        self.as_ref().get_rssi()
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

    fn get_rssi(&self) -> Decibels {
        unsafe { otPlatRadioGetRssi(self.as_ot_ptr()) }
    }
}
