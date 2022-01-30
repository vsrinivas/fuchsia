// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Methods from the [OpenThread "OperationalDataset" Module][1].
///
/// [1]: https://openthread.io/reference/group/api-operational-dataset
pub trait Dataset {
    /// Functional equivalent of
    /// [`otsys::otDatasetIsCommissioned`](crate::otsys::otDatasetIsCommissioned).
    #[must_use]
    fn is_commissioned(&self) -> bool;

    /// Functional equivalent of
    /// [`otsys::otDatasetCreateNewNetwork`](crate::otsys::otDatasetCreateNewNetwork).
    fn dataset_create_new_network(&self, dataset: &mut OperationalDataset) -> Result;

    /// Functional equivalent of [`otsys::otDatasetGetActive`](crate::otsys::otDatasetGetActive).
    fn dataset_get_active(&self, dataset: &mut OperationalDataset) -> Result;

    /// Functional equivalent of [`otsys::otDatasetSetActive`](crate::otsys::otDatasetSetActive).
    fn dataset_set_active(&self, dataset: &OperationalDataset) -> Result;

    /// Functional equivalent of [`otsys::otDatasetSetPending`](crate::otsys::otDatasetSetPending).
    fn dataset_set_pending(&self, dataset: &OperationalDataset) -> Result;

    /// Functional equivalent of [`otsys::otDatasetGetActiveTlvs`](crate::otsys::otDatasetGetActiveTlvs).
    fn dataset_get_active_tlvs(&self) -> Result<OperationalDatasetTlvs>;

    /// Functional equivalent of [`otsys::otDatasetSetActiveTlvs`](crate::otsys::otDatasetSetActiveTlvs).
    fn dataset_set_active_tlvs(&self, dataset: &OperationalDatasetTlvs) -> Result;

    /// Functional equivalent of [`otsys::otDatasetGetPendingTlvs`](crate::otsys::otDatasetGetPendingTlvs).
    fn dataset_get_pending_tlvs(&self) -> Result<OperationalDatasetTlvs>;

    /// Functional equivalent of [`otsys::otDatasetSetPendingTlvs`](crate::otsys::otDatasetSetPendingTlvs).
    fn dataset_set_pending_tlvs(&self, dataset: &OperationalDatasetTlvs) -> Result;
}

impl<T: Dataset + Boxable> Dataset for ot::Box<T> {
    fn is_commissioned(&self) -> bool {
        self.as_ref().is_commissioned()
    }

    fn dataset_create_new_network(&self, dataset: &mut OperationalDataset) -> Result {
        self.as_ref().dataset_create_new_network(dataset)
    }

    fn dataset_get_active(&self, dataset: &mut OperationalDataset) -> Result {
        self.as_ref().dataset_get_active(dataset)
    }

    fn dataset_set_active(&self, dataset: &OperationalDataset) -> Result {
        self.as_ref().dataset_set_active(dataset)
    }

    fn dataset_set_pending(&self, dataset: &OperationalDataset) -> Result {
        self.as_ref().dataset_set_pending(dataset)
    }

    fn dataset_get_active_tlvs(&self) -> Result<OperationalDatasetTlvs> {
        self.as_ref().dataset_get_active_tlvs()
    }

    fn dataset_set_active_tlvs(&self, dataset: &OperationalDatasetTlvs) -> Result {
        self.as_ref().dataset_set_active_tlvs(dataset)
    }

    fn dataset_get_pending_tlvs(&self) -> Result<OperationalDatasetTlvs> {
        self.as_ref().dataset_get_pending_tlvs()
    }

    fn dataset_set_pending_tlvs(&self, dataset: &OperationalDatasetTlvs) -> Result {
        self.as_ref().dataset_set_pending_tlvs(dataset)
    }
}

impl Dataset for Instance {
    fn is_commissioned(&self) -> bool {
        unsafe { otDatasetIsCommissioned(self.as_ot_ptr()) }
    }

    fn dataset_create_new_network(&self, dataset: &mut OperationalDataset) -> Result {
        Error::from(unsafe { otDatasetCreateNewNetwork(self.as_ot_ptr(), dataset.as_ot_mut_ptr()) })
            .into()
    }

    fn dataset_get_active(&self, dataset: &mut OperationalDataset) -> Result {
        Error::from(unsafe { otDatasetGetActive(self.as_ot_ptr(), dataset.as_ot_mut_ptr()) }).into()
    }

    fn dataset_set_active(&self, dataset: &OperationalDataset) -> Result {
        Error::from(unsafe { otDatasetSetActive(self.as_ot_ptr(), dataset.as_ot_ptr()) }).into()
    }

    fn dataset_set_pending(&self, dataset: &OperationalDataset) -> Result {
        Error::from(unsafe { otDatasetSetPending(self.as_ot_ptr(), dataset.as_ot_ptr()) }).into()
    }

    fn dataset_get_active_tlvs(&self) -> Result<OperationalDatasetTlvs> {
        let mut ret = OperationalDatasetTlvs::default();

        Error::from(unsafe { otDatasetGetActiveTlvs(self.as_ot_ptr(), ret.as_ot_mut_ptr()) })
            .into_result()?;

        Ok(ret)
    }

    fn dataset_set_active_tlvs(&self, dataset: &OperationalDatasetTlvs) -> Result {
        Error::from(unsafe { otDatasetSetActiveTlvs(self.as_ot_ptr(), dataset.as_ot_ptr()) }).into()
    }

    fn dataset_get_pending_tlvs(&self) -> Result<OperationalDatasetTlvs> {
        let mut ret = OperationalDatasetTlvs::default();

        Error::from(unsafe { otDatasetGetPendingTlvs(self.as_ot_ptr(), ret.as_ot_mut_ptr()) })
            .into_result()?;

        Ok(ret)
    }

    fn dataset_set_pending_tlvs(&self, dataset: &OperationalDatasetTlvs) -> Result {
        Error::from(unsafe { otDatasetSetPendingTlvs(self.as_ot_ptr(), dataset.as_ot_ptr()) })
            .into()
    }
}
