// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_bluetooth_bredr::ServiceClassProfileIdentifier,
    fuchsia_bluetooth::types::PeerId,
    fuchsia_syslog::fx_log_warn,
    slab::Slab,
    std::collections::{HashMap, HashSet},
};

use crate::types::{Psm, RegisteredServiceId, ServiceRecord};

/// The unique handle assigned to a group of registered services.
/// Multiple services that are registered together will be assigned
/// the same RegistrationHandle.
pub type RegistrationHandle = usize;

/// The unique handle assigned to each registered service.
pub type ServiceHandle = usize;

/// Handles the registration and unregistration of services. Stores the registered
/// services as `ServiceRecords` and provides convenience methods to access
/// services indexed by their ServiceClassProfileIdentifier.
/// Despite similarities, `ServiceSet` does not conform to the SDP protocol,
/// and provides no guarantees in that regard.
pub struct ServiceSet {
    /// The Peer Id that this `ServiceSet` represents.
    peer_id: PeerId,

    /// There is one RegistrationHandle assigned to a set of services that are
    /// registered together.
    reg_to_service: HashMap<RegistrationHandle, HashSet<ServiceHandle>>,

    /// Each service, stored as a ServiceRecord, is assigned a unique ServiceHandle.
    records: Slab<ServiceRecord>,

    /// A single PSM can be specified by multiple services.
    psm_to_services: HashMap<Psm, HashSet<ServiceHandle>>,

    /// The ServiceClassIds supported by each registration. This is used to speed
    /// up the matching to a service search.
    reg_to_svc_ids: HashMap<RegistrationHandle, HashSet<ServiceClassProfileIdentifier>>,
}

impl ServiceSet {
    pub fn new(peer_id: PeerId) -> Self {
        Self {
            peer_id,
            reg_to_service: HashMap::new(),
            records: Slab::new(),
            psm_to_services: HashMap::new(),
            reg_to_svc_ids: HashMap::new(),
        }
    }

    /// Returns a HashSet of ServiceClassProfileIds that are registered to a particular
    /// RegistrationHandle.
    pub fn get_service_ids_for_registration_handle(
        &self,
        handle: &RegistrationHandle,
    ) -> Option<&HashSet<ServiceClassProfileIdentifier>> {
        self.reg_to_svc_ids.get(handle)
    }

    /// Checks if the provided `psm` is registered by any of the services.
    ///
    /// Returns the RegistrationHandle of the service specifying the PSM, or
    /// None if not registered.
    pub fn psm_registered(&self, psm: Psm) -> Option<RegistrationHandle> {
        for (reg_handle, handles) in &self.reg_to_service {
            for service_handle in handles {
                if let Some(record) = self.records.get(service_handle.clone()) {
                    if record.contains_psm(&psm) {
                        return Some(reg_handle.clone());
                    }
                }
            }
        }
        None
    }

    /// Returns a map of ServiceRecords (if any), that conform to the provided Service Class `ids`.
    /// Convenience function for speeding up the matching process.
    pub fn get_service_records(
        &self,
        ids: &HashSet<ServiceClassProfileIdentifier>,
    ) -> HashMap<ServiceClassProfileIdentifier, Vec<ServiceRecord>> {
        let mut records_map = HashMap::new();
        for (_, record) in &self.records {
            for id in ids {
                if record.contains_service_class_identifier(id) {
                    records_map.entry(id.clone()).or_insert(Vec::new()).push(record.clone());
                }
            }
        }
        records_map
    }

    /// Attempts to register the group of services specified by `records`.
    ///
    /// Returns None if:
    ///   1) No service records are provided.
    ///   2) A ServiceRecord contains an already registered PSM.
    ///
    /// Assigns a unique ServiceHandle to each registered ServiceRecord.
    ///
    /// Returns a RegistrationHandle, uniquely identifying the services that were registered
    /// together. The returned handle should be used to unregister the group of services using
    /// Self::unregister_service().
    pub fn register_service(&mut self, records: Vec<ServiceRecord>) -> Option<RegistrationHandle> {
        if records.is_empty() {
            return None;
        }

        // Any service in `records` must not request a psm in `existing_psms`.
        let existing_psms = self.psm_to_services.keys().cloned().collect();
        for record in &records {
            // The requested PSMs must not be registered already.
            if !record.is_disjoint(&existing_psms) {
                fx_log_warn!("PSM already registered");
                return None;
            }
        }

        let mut assigned_handles = HashSet::new();
        let mut service_class_ids: HashSet<ServiceClassProfileIdentifier> = HashSet::new();

        // Each service is valid now. Register each record and store the relevant metadata.
        for mut record in records {
            let entry = self.records.vacant_entry();
            let next: ServiceHandle = entry.key();

            // Register the ServiceRecord with the unique PeerId,ServiceHandle combination.
            record.register_service_record(RegisteredServiceId::new(self.peer_id, next));

            // Save the (psm, handle) mappings.
            for psm in record.psms() {
                self.psm_to_services.entry(psm.clone()).or_insert(HashSet::new()).insert(next);
            }

            // Update the set of ServiceClassIds, save the assigned handle, and store the
            // ServiceRecord.
            service_class_ids = service_class_ids.union(record.service_ids()).cloned().collect();
            assigned_handles.insert(next);
            entry.insert(record);
        }

        // The RegistrationHandle is the min of the (nonempty) `assigned_handles` set.
        let registration_handle = assigned_handles.iter().min().cloned().unwrap();
        self.reg_to_svc_ids.insert(registration_handle, service_class_ids);
        self.reg_to_service.insert(registration_handle, assigned_handles);

        Some(registration_handle)
    }

    /// Unregisters the service(s) associated with the RegistrationHandle `handle`.
    ///
    /// Returns true if any services were unregistered.
    pub fn unregister_service(&mut self, handle: &RegistrationHandle) -> bool {
        let removed = self.reg_to_service.remove(handle);
        if removed.is_none() {
            return false;
        }

        self.reg_to_svc_ids.remove(handle);

        for svc_handle in removed.expect("just checked") {
            if self.records.contains(svc_handle.clone()) {
                let psms = self.records.remove(svc_handle.clone()).psms();
                for psm in psms {
                    self.psm_to_services.remove(&psm);
                }
            }
        }

        true
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    // Builds a ServiceRecord with the provided `psm`.
    pub(crate) fn build_a2dp_service_record(psm: Psm) -> ServiceRecord {
        let mut service_ids = HashSet::new();
        service_ids.insert(ServiceClassProfileIdentifier::AudioSink);
        service_ids.insert(ServiceClassProfileIdentifier::AudioSource);

        ServiceRecord::new(service_ids, Some(psm), HashSet::new(), vec![], vec![])
    }

    fn build_avrcp_service_record(psm: Psm) -> ServiceRecord {
        let mut service_ids = HashSet::new();
        service_ids.insert(ServiceClassProfileIdentifier::AvRemoteControl);
        service_ids.insert(ServiceClassProfileIdentifier::AvRemoteControlController);

        ServiceRecord::new(service_ids, Some(psm), HashSet::new(), vec![], vec![])
    }

    #[test]
    fn test_register_service_success() {
        let id = PeerId(123);
        let mut manager = ServiceSet::new(id);

        // Single, valid record, is successful.
        let psm0 = Psm(19);
        let single_record = vec![build_a2dp_service_record(psm0)];
        let reg_handle = manager.register_service(single_record);
        assert!(reg_handle.is_some());
        assert_eq!(reg_handle, manager.psm_registered(psm0));

        // The relevant ServiceClassProfileIds should be registered for this handle.
        let mut expected_ids = HashSet::new();
        expected_ids.insert(ServiceClassProfileIdentifier::AudioSink);
        expected_ids.insert(ServiceClassProfileIdentifier::AudioSource);
        let service_ids =
            manager.get_service_ids_for_registration_handle(&reg_handle.unwrap()).unwrap();
        assert_eq!(expected_ids, service_ids.clone());

        // Multiple, valid records, is successful.
        let psm1 = Psm(20);
        let psm2 = Psm(21);
        let records = vec![build_a2dp_service_record(psm1), build_a2dp_service_record(psm2)];
        let reg_handle2 = manager.register_service(records);
        assert!(reg_handle2.is_some());
        assert_eq!(reg_handle2, manager.psm_registered(psm1));
        assert_eq!(reg_handle2, manager.psm_registered(psm2));

        // Multiple records with overlapping PSMs is successful since they are registered together.
        let (psm3, psm4) = (Psm(22), Psm(23));
        let overlapping = vec![
            build_avrcp_service_record(psm3),
            build_avrcp_service_record(psm3),
            build_a2dp_service_record(psm4),
        ];
        let reg_handle3 = manager.register_service(overlapping);
        assert!(reg_handle3.is_some());
        assert_eq!(reg_handle3, manager.psm_registered(psm3));
        assert_eq!(reg_handle3, manager.psm_registered(psm4));

        expected_ids.insert(ServiceClassProfileIdentifier::AvRemoteControl);
        expected_ids.insert(ServiceClassProfileIdentifier::AvRemoteControlController);
        let service_ids = manager.get_service_ids_for_registration_handle(&reg_handle3.unwrap());
        assert!(service_ids.is_some());
        let service_ids = service_ids.unwrap();
        assert_eq!(&expected_ids, service_ids);

        // Unregistering a service should succeed. Only the relevant PSMs should be removed.
        assert!(manager.unregister_service(&reg_handle3.unwrap()));
        assert_eq!(reg_handle, manager.psm_registered(psm0));
        assert_eq!(reg_handle2, manager.psm_registered(psm1));
        assert_eq!(reg_handle2, manager.psm_registered(psm2));
        assert_eq!(None, manager.psm_registered(psm3));
        assert_eq!(None, manager.psm_registered(psm4));

        // Unregistering the same service shouldn't do anything.
        assert_eq!(false, manager.unregister_service(&reg_handle3.unwrap()));
    }

    #[test]
    fn test_register_service_error() {
        let id = PeerId(123);
        let mut manager = ServiceSet::new(id);

        // Empty ServiceRecords is invalid
        let empty_records = vec![];
        assert_eq!(None, manager.register_service(empty_records));
        assert!(manager.get_service_records(&HashSet::new()).is_empty());

        // Attempting to register the same PSM fails the second time.
        let psm = Psm(19);
        let single_record = vec![build_a2dp_service_record(psm)];
        assert!(manager.register_service(single_record).is_some());
        let duplicate_record = vec![build_a2dp_service_record(psm)];
        assert_eq!(None, manager.register_service(duplicate_record));
    }
}
