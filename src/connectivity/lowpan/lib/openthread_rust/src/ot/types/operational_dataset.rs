// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;
use std::ops::Deref;

/// Functional equivalent of [`otsys::otOperationalDataset`](crate::otsys::otOperationalDataset).
#[derive(Default, Clone)]
#[repr(transparent)]
pub struct OperationalDataset(pub otOperationalDataset);

impl_ot_castable!(OperationalDataset, otOperationalDataset);

impl OperationalDataset {
    /// Returns an empty operational dataset.
    pub fn empty() -> OperationalDataset {
        Self::default()
    }

    /// Returns true if this dataset is considered "complete"
    pub fn is_complete(&self) -> bool {
        self.get_active_timestamp().is_some()
            && self.get_network_name().is_some()
            && self.get_network_key().is_some()
            && self.get_extended_pan_id().is_some()
            && self.get_mesh_local_prefix().is_some()
            && self.get_pan_id().is_some()
            && self.get_channel().is_some()
    }

    // TODO: Not clear what the OpenThread API is to accomplish this.
    // pub fn to_tlvs(&self) -> OperationalDatasetTlvs {
    //     todo!()
    // }
}

impl std::fmt::Debug for OperationalDataset {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        let mut ds = f.debug_struct("OperationalDataset");

        if let Some(x) = self.get_network_name() {
            ds.field("network_name", &x);
        }
        if let Some(x) = self.get_extended_pan_id() {
            ds.field("xpanid", &x);
        }
        if let Some(x) = self.get_network_key() {
            ds.field("network_key", &x);
        }
        if let Some(x) = self.get_mesh_local_prefix() {
            ds.field("mesh_local_prefix", &x);
        }
        if let Some(x) = self.get_pan_id() {
            ds.field("panid", &x);
        }
        if let Some(x) = self.get_channel() {
            ds.field("channel", &x);
        }
        if let Some(x) = self.get_channel_mask() {
            ds.field("channel_mask", &x);
        }
        if let Some(x) = self.get_pskc() {
            ds.field("pskc", &x);
        }
        if let Some(x) = self.get_security_policy() {
            ds.field("security_policy", &x);
        }
        if let Some(x) = self.get_delay() {
            ds.field("delay", &x);
        }
        if let Some(x) = self.get_active_timestamp() {
            ds.field("active_timestamp", &x);
        }
        if let Some(x) = self.get_pending_timestamp() {
            ds.field("pending_timestamp", &x);
        }
        ds.finish()
    }
}

impl OperationalDataset {
    /// Returns the channel index, if present.
    pub fn get_channel(&self) -> Option<ChannelIndex> {
        self.0.mComponents.mIsChannelPresent().then(|| self.0.mChannel.try_into().unwrap())
    }

    /// Returns the channel mask, if present.
    pub fn get_channel_mask(&self) -> Option<ChannelMask> {
        self.0.mComponents.mIsChannelMaskPresent().then(|| self.0.mChannelMask.into())
    }

    /// Returns the delay, if present.
    pub fn get_delay(&self) -> Option<u32> {
        self.0.mComponents.mIsDelayPresent().then(|| self.0.mDelay)
    }

    /// Returns the extended PAN-ID, if present.
    pub fn get_extended_pan_id(&self) -> Option<&ExtendedPanId> {
        self.0
            .mComponents
            .mIsExtendedPanIdPresent()
            .then(|| ExtendedPanId::ref_from_ot_ref(&self.0.mExtendedPanId))
    }

    /// Returns the network key, if present.
    pub fn get_network_key(&self) -> Option<&NetworkKey> {
        self.0
            .mComponents
            .mIsNetworkKeyPresent()
            .then(|| NetworkKey::ref_from_ot_ref(&self.0.mNetworkKey))
    }

    /// Returns the network key, if present.
    pub fn get_pskc(&self) -> Option<&Pskc> {
        self.0.mComponents.mIsPskcPresent().then(|| Pskc::ref_from_ot_ref(&self.0.mPskc))
    }

    /// Returns the network name, if present.
    pub fn get_network_name(&self) -> Option<&NetworkName> {
        self.0
            .mComponents
            .mIsNetworkNamePresent()
            .then(|| NetworkName::ref_from_ot_ref(&self.0.mNetworkName))
    }

    /// Returns the PAN-ID, if present.
    pub fn get_pan_id(&self) -> Option<PanId> {
        self.0.mComponents.mIsPanIdPresent().then(|| self.0.mPanId)
    }

    /// Returns the active timestamp, if present.
    pub fn get_active_timestamp(&self) -> Option<u64> {
        self.0.mComponents.mIsActiveTimestampPresent().then(|| self.0.mActiveTimestamp)
    }

    /// Returns the pending timestamp, if present.
    pub fn get_pending_timestamp(&self) -> Option<u64> {
        self.0.mComponents.mIsPendingTimestampPresent().then(|| self.0.mPendingTimestamp)
    }

    /// Returns the security policy, if present.
    pub fn get_security_policy(&self) -> Option<&SecurityPolicy> {
        self.0
            .mComponents
            .mIsSecurityPolicyPresent()
            .then(|| SecurityPolicy::ref_from_ot_ref(&self.0.mSecurityPolicy))
    }

    /// Returns the mesh-local prefix, if present.
    pub fn get_mesh_local_prefix(&self) -> Option<&MeshLocalPrefix> {
        self.0
            .mComponents
            .mIsMeshLocalPrefixPresent()
            .then(|| &self.0.mMeshLocalPrefix)
            .map(Into::into)
    }
}

impl OperationalDataset {
    /// Sets or clears the channel index.
    pub fn set_channel(&mut self, opt: Option<ChannelIndex>) {
        if let Some(x) = opt {
            self.0.mChannel = x.into();
            self.0.mComponents.set_mIsChannelPresent(true);
        } else {
            self.0.mComponents.set_mIsChannelPresent(false);
        }
    }

    /// Sets or clears the channel mask.
    pub fn set_channel_mask(&mut self, opt: Option<ChannelMask>) {
        if let Some(x) = opt {
            self.0.mChannelMask = x.into();
            self.0.mComponents.set_mIsChannelMaskPresent(true);
        } else {
            self.0.mComponents.set_mIsChannelMaskPresent(false);
        }
    }

    /// Sets or clears the delay.
    pub fn set_delay(&mut self, opt: Option<u32>) {
        if let Some(x) = opt {
            self.0.mDelay = x;
            self.0.mComponents.set_mIsDelayPresent(true);
        } else {
            self.0.mComponents.set_mIsDelayPresent(false);
        }
    }

    /// Sets or clears the extended PAN-ID.
    pub fn set_extended_pan_id(&mut self, opt: Option<&ExtendedPanId>) {
        if let Some(x) = opt {
            self.0.mExtendedPanId = x.as_ot_ref().clone();
            self.0.mComponents.set_mIsExtendedPanIdPresent(true);
        } else {
            self.0.mComponents.set_mIsExtendedPanIdPresent(false);
        }
    }

    /// Sets or clears the network key.
    pub fn set_network_key(&mut self, opt: Option<&NetworkKey>) {
        if let Some(x) = opt {
            self.0.mNetworkKey = x.as_ot_ref().clone();
            self.0.mComponents.set_mIsNetworkKeyPresent(true);
        } else {
            self.0.mComponents.set_mIsNetworkKeyPresent(false);
        }
    }

    /// Sets or clears the network name.
    pub fn set_network_name(&mut self, opt: Option<&NetworkName>) {
        if let Some(x) = opt {
            self.0.mNetworkName = x.as_ot_ref().clone();
            self.0.mComponents.set_mIsNetworkNamePresent(true);
        } else {
            self.0.mComponents.set_mIsNetworkNamePresent(false);
        }
    }

    /// Sets or clears the PAN-ID.
    pub fn set_pan_id(&mut self, opt: Option<PanId>) {
        if let Some(x) = opt {
            self.0.mPanId = x;
            self.0.mComponents.set_mIsPanIdPresent(true);
        } else {
            self.0.mComponents.set_mIsPanIdPresent(false);
        }
    }

    /// Sets or clears the active timestamp
    pub fn set_active_timestamp(&mut self, opt: Option<u64>) {
        if let Some(x) = opt {
            self.0.mActiveTimestamp = x.into();
            self.0.mComponents.set_mIsActiveTimestampPresent(true);
        } else {
            self.0.mComponents.set_mIsActiveTimestampPresent(false);
        }
    }

    /// Sets or clears the pending timestamp.
    pub fn set_pending_timestamp(&mut self, opt: Option<u64>) {
        if let Some(x) = opt {
            self.0.mPendingTimestamp = x;
            self.0.mComponents.set_mIsPendingTimestampPresent(true);
        } else {
            self.0.mComponents.set_mIsPendingTimestampPresent(false);
        }
    }

    /// Sets or clears the security policy.
    pub fn set_security_policy(&mut self, opt: Option<SecurityPolicy>) {
        if let Some(x) = opt {
            self.0.mSecurityPolicy = x.as_ot_ref().clone();
            self.0.mComponents.set_mIsSecurityPolicyPresent(true);
        } else {
            self.0.mComponents.set_mIsSecurityPolicyPresent(false);
        }
    }

    /// Sets or clears the mesh-local prefix.
    pub fn set_mesh_local_prefix(&mut self, opt: Option<&MeshLocalPrefix>) {
        if let Some(x) = opt {
            self.0.mMeshLocalPrefix = x.as_ot_ref().clone();
            self.0.mComponents.set_mIsMeshLocalPrefixPresent(true);
        } else {
            self.0.mComponents.set_mIsMeshLocalPrefixPresent(false);
        }
    }
}

/// Functional equivalent of [`otsys::otOperationalDatasetTlvs`](crate::otsys::otOperationalDatasetTlvs).
#[derive(Debug, Default, Clone)]
#[repr(transparent)]
pub struct OperationalDatasetTlvs(pub otOperationalDatasetTlvs);

impl_ot_castable!(OperationalDatasetTlvs, otOperationalDatasetTlvs);

impl OperationalDatasetTlvs {
    /// Tries to parse the TLVs into a dataset
    /// Functional equivalent to `otDatasetParseTlvs`
    pub fn try_to_dataset(&self) -> Result<OperationalDataset> {
        let mut ret = OperationalDataset::default();
        Error::from(unsafe { otDatasetParseTlvs(self.as_ot_ptr(), ret.as_ot_mut_ptr()) })
            .into_result()?;
        Ok(ret)
    }

    /// Tries to create a `OperationalDatasetTlvs` instance from the given byte slice.
    pub fn try_from_slice(slice: &[u8]) -> Result<Self, ot::WrongSize> {
        let mut ret = Self::default();
        let len = slice.len();

        if len > OT_OPERATIONAL_DATASET_MAX_LENGTH as usize {
            return Err(ot::WrongSize);
        }

        ret.0.mLength = len.try_into().unwrap();

        ret.0.mTlvs[0..len].clone_from_slice(slice);

        Ok(ret)
    }

    /// Returns length of the TLVs in bytes. 0-16.
    pub fn len(&self) -> usize {
        self.0.mLength as usize
    }

    /// Returns the TLVs as a byte slice with no trailing zeros.
    pub fn as_slice(&self) -> &[u8] {
        &self.0.mTlvs[0..self.len()]
    }

    /// Creates a `Vec<u8>` from the raw bytes of the TLVs
    pub fn to_vec(&self) -> Vec<u8> {
        self.as_slice().to_vec()
    }
}

impl Deref for OperationalDatasetTlvs {
    type Target = [u8];

    fn deref(&self) -> &Self::Target {
        self.as_slice()
    }
}

impl<'a> TryFrom<&'a [u8]> for OperationalDatasetTlvs {
    type Error = ot::WrongSize;

    fn try_from(value: &'a [u8]) -> Result<Self, Self::Error> {
        OperationalDatasetTlvs::try_from_slice(value)
    }
}

impl TryFrom<Vec<u8>> for OperationalDatasetTlvs {
    type Error = ot::WrongSize;

    fn try_from(value: Vec<u8>) -> Result<Self, Self::Error> {
        OperationalDatasetTlvs::try_from_slice(&value)
    }
}

impl From<OperationalDatasetTlvs> for Vec<u8> {
    fn from(value: OperationalDatasetTlvs) -> Self {
        value.to_vec()
    }
}

impl TryFrom<OperationalDatasetTlvs> for OperationalDataset {
    type Error = ot::Error;

    fn try_from(value: OperationalDatasetTlvs) -> Result<Self, Self::Error> {
        value.try_to_dataset()
    }
}
