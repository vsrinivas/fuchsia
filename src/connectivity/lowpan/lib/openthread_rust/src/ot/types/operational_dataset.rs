// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Functional equivalent of [`otsys::otOperationalDataset`](crate::otsys::otOperationalDataset).
#[derive(Debug, Default, Clone)]
#[repr(transparent)]
pub struct OperationalDataset(pub otOperationalDataset);

impl_ot_castable!(OperationalDataset, otOperationalDataset);

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
