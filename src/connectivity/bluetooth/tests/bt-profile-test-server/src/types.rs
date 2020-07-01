// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Error},
    fidl_fuchsia_bluetooth_bredr::{
        self as fidl_profile, Attribute, DataElement, ProfileDescriptor, ProtocolDescriptor,
        ProtocolIdentifier, ServiceClassProfileIdentifier,
    },
    fuchsia_bluetooth::types::{PeerId, Uuid},
    std::collections::HashSet,
};

use crate::peer::service::ServiceHandle;

/// The Protocol Service Multiplexer (PSM) for L2cap connections.
#[derive(Copy, Clone, Debug, Hash, Eq, PartialEq)]
pub struct Psm(pub u16);

/// Convenience type for storing the fields of a Profile.ServiceFound response.
pub struct ServiceFoundResponse {
    pub id: PeerId,
    pub protocol: Option<Vec<ProtocolDescriptor>>,
    pub attributes: Vec<Attribute>,
}

/// The unique identifier associated with a registered service.
/// This ID is unique in the _entire_ piconet because there is at most
/// one peer with a unique PeerId, and each service registered by the peer
/// is uniquely identified by a ServiceHandle. Therefore, the combination
/// of PeerId and ServiceHandle can uniquely identify a service.
/// This is used to prevent duplication of messages when reporting services
/// to a search.
#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
pub struct RegisteredServiceId(PeerId, ServiceHandle);

impl RegisteredServiceId {
    pub fn new(id: PeerId, handle: ServiceHandle) -> Self {
        Self(id, handle)
    }
}

/// A ServiceRecord representing the information about a service.
/// A ServiceRecord is considered "registered" when it has been assigned
/// a unique RegisteredServiceId.
// TODO(51454): Store all the fields of the ServiceDefinition here.
#[derive(Clone, Debug, PartialEq)]
pub struct ServiceRecord {
    /// The Service Class IDs specified by this record. There must be at least one.
    svc_ids: HashSet<ServiceClassProfileIdentifier>,

    /// The PSM specified by this record. It is valid to not specify a primary PSM.
    primary_psm: Option<Psm>,

    /// Any additional PSMs specified by this record. If none, this will be empty.
    additional_psms: HashSet<Psm>,

    /// The ProfileDescriptors specified by this record.
    profile_descriptors: Vec<ProfileDescriptor>,

    /// Metadata about this service. This information will be set when the service
    /// is registered. Use `ServiceRecord::register_service_record()` to mark the
    /// ServiceRecord as registered by assigning a unique `RegisteredServiceId`.
    reg_id: Option<RegisteredServiceId>,
}

impl ServiceRecord {
    pub fn new(
        svc_ids: HashSet<ServiceClassProfileIdentifier>,
        primary_psm: Option<Psm>,
        additional_psms: HashSet<Psm>,
        profile_descriptors: Vec<ProfileDescriptor>,
    ) -> Self {
        Self { svc_ids, primary_psm, additional_psms, profile_descriptors, reg_id: None }
    }

    /// Returns the ServiceHandle associated with this ServiceRecord, if set.
    pub fn handle(&self) -> Option<ServiceHandle> {
        self.reg_id.map(|id| id.1)
    }

    pub fn service_ids(&self) -> &HashSet<ServiceClassProfileIdentifier> {
        &self.svc_ids
    }

    /// Returns all the PSMs specified by this record.
    pub fn psms(&self) -> HashSet<Psm> {
        let mut psms = HashSet::new();
        if let Some(psm) = self.primary_psm {
            psms.insert(psm);
        }
        psms.union(&self.additional_psms).cloned().collect()
    }

    /// Returns true if the Service has been registered. Namely, it must be assigned
    /// a PeerId and ServiceHandle.
    #[cfg(test)]
    fn is_registered(&self) -> bool {
        self.reg_id.is_some()
    }

    /// Every registered ServiceRecord has a unique identification. This is simply
    /// the PeerId and ServiceHandle associated with the ServiceRecord.
    ///
    /// This is guaranteed to be unique because there will only ever be one mock peer registered per
    /// `peer_id` and any services for the mock peer will have a unique ServiceHandle.
    ///
    /// Returns an error if the service has not been registered.
    pub fn unique_service_id(&self) -> Result<RegisteredServiceId, Error> {
        self.reg_id.ok_or(format_err!("The ServiceRecord has not been registered"))
    }

    /// Returns true if the provided `psm` is specified by this record.
    pub fn contains_psm(&self, psm: &Psm) -> bool {
        self.primary_psm.map_or(false, |primary| &primary == psm)
            || self.additional_psms.contains(psm)
    }

    /// Returns true if the provided `id` is specified by this record.
    pub fn contains_service_class_identifier(&self, id: &ServiceClassProfileIdentifier) -> bool {
        self.svc_ids.contains(id)
    }

    /// Returns true if the PSMs specified by this record do not overlap with
    /// the PSMs in `other`.
    pub fn is_disjoint(&self, other: &HashSet<Psm>) -> bool {
        self.primary_psm.map_or(true, |psm| !other.contains(&psm))
            && self.additional_psms.is_disjoint(other)
    }

    /// Marks the ServiceRecord as registered by assigning it a unique `reg_id`.
    pub fn register_service_record(&mut self, reg_id: RegisteredServiceId) {
        self.reg_id = Some(reg_id);
    }

    /// Converts the ServiceRecord into a ServiceFoundResponse. Builds the
    /// ProtocolDescriptorList from the data in the ServiceRecord.
    ///
    /// Returns an error if the ServiceRecord has not been registered.
    // TODO(51454): Build the full ServiceFoundResponse. Right now, we just
    // build the primary L2CAP Protocol, ServiceClassIdentifiers, and Profile Descriptors.
    // TODO(51454): Filter response to only include attributes requested by
    // the search. Right now we just return everything regardless - this is OK
    // according to Profile API.
    pub fn to_service_found_response(&self) -> Result<ServiceFoundResponse, Error> {
        let peer_id = self.reg_id.ok_or(format_err!("The service has not been registered."))?.0;
        let mut attributes = vec![];

        // 1. Build the (optional) primary Protocol Descriptor List. This is both returned and included
        //    in `attributes`.
        let prot_list = if let Some(Psm(psm)) = self.primary_psm {
            let prot_list = vec![
                Some(Box::new(Uuid::new16(ProtocolIdentifier::L2Cap as u16).into())),
                Some(Box::new(DataElement::Uint16(psm))),
            ];
            attributes.push(Attribute {
                id: fidl_profile::ATTR_PROTOCOL_DESCRIPTOR_LIST,
                element: DataElement::Sequence(vec![Some(Box::new(DataElement::Sequence(
                    prot_list,
                )))]),
            });

            Some(vec![ProtocolDescriptor {
                protocol: ProtocolIdentifier::L2Cap,
                params: vec![DataElement::Uint16(psm)],
            }])
        } else {
            None
        };

        // 2. Add the Service Class ID List Attribute. There should always be at least one.
        let svc_ids_list: Vec<ServiceClassProfileIdentifier> =
            self.svc_ids.iter().cloned().collect();
        let svc_ids_sequence =
            svc_ids_list.into_iter().map(|id| Some(Box::new(Uuid::from(id).into()))).collect();
        attributes.push(Attribute {
            id: fidl_profile::ATTR_SERVICE_CLASS_ID_LIST,
            element: DataElement::Sequence(svc_ids_sequence),
        });

        // 3. Add the potential Profile Descriptors.
        if !self.profile_descriptors.is_empty() {
            let mut prof_desc_sequence = vec![];
            for descriptor in &self.profile_descriptors {
                let desc_list = vec![
                    Some(Box::new(Uuid::from(descriptor.profile_id).into())),
                    Some(Box::new(DataElement::Uint16(u16::from_be_bytes([
                        descriptor.major_version,
                        descriptor.minor_version,
                    ])))),
                ];
                prof_desc_sequence.push(Some(Box::new(DataElement::Sequence(desc_list))));
            }
            attributes.push(Attribute {
                id: fidl_profile::ATTR_BLUETOOTH_PROFILE_DESCRIPTOR_LIST,
                element: DataElement::Sequence(prof_desc_sequence),
            });
        }

        Ok(ServiceFoundResponse { id: peer_id, protocol: prot_list, attributes })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Tests operations on a ServiceRecord.
    #[test]
    fn test_service_record() {
        let mut additional = HashSet::new();
        additional.insert(Psm(10));
        let mut ids = HashSet::new();
        ids.insert(ServiceClassProfileIdentifier::AudioSource);
        let descs = vec![ProfileDescriptor {
            profile_id: ServiceClassProfileIdentifier::AudioSource,
            major_version: 1,
            minor_version: 2,
        }];
        let mut service_record = ServiceRecord::new(ids, Some(Psm(20)), additional, descs);

        // Creating the initial ServiceRecord should not be registered.
        assert!(service_record.contains_psm(&Psm(20)));
        assert_eq!(false, service_record.is_registered());
        assert!(service_record
            .contains_service_class_identifier(&ServiceClassProfileIdentifier::AudioSource));
        let mut expected_psms = HashSet::new();
        expected_psms.insert(Psm(10));
        expected_psms.insert(Psm(20));
        assert_eq!(expected_psms, service_record.psms());

        let mut random_psms = HashSet::new();
        random_psms.insert(Psm(14));
        random_psms.insert(Psm(19));
        assert!(service_record.is_disjoint(&random_psms));
        random_psms.insert(Psm(20));
        assert!(!service_record.is_disjoint(&random_psms));

        // Register the record, as ServiceManager would, by updating the unique handles.
        let handle: RegisteredServiceId = RegisteredServiceId(PeerId(123), 99);
        service_record.register_service_record(handle);
        assert_eq!(true, service_record.is_registered());

        // TODO(55461): Validate the results of ServiceRecord::to_service_found().
    }
}
