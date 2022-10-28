// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use bitfield::bitfield;
use fidl_fuchsia_bluetooth_bredr as bredr;
use fidl_fuchsia_bluetooth_deviceid as di;
use fuchsia_bluetooth::types::Uuid;
use std::convert::TryFrom;

use crate::error::Error;

/// Integer attribute values specified in the Device Identification specification.
/// See DI v1.3, Section 5.
const ATTR_SPECIFICATION_ID: u16 = 0x0200;
const ATTR_VENDOR_ID: u16 = 0x0201;
const ATTR_PRODUCT_ID: u16 = 0x0202;
const ATTR_VERSION: u16 = 0x0203;
const ATTR_PRIMARY_RECORD: u16 = 0x0204;
const ATTR_VENDOR_ID_SOURCE: u16 = 0x0205;
const ATTR_DOCUMENTATION_URL: u16 = 0x000A;

/// This implementation supports DI v1.3.
const DEVICE_IDENTIFICATION_PROFILE_VERSION: u16 = 0x0103;
/// URL string representing the generic landing page of Fuchsia.
const FUCHSIA_DOCUMENTATION_URL: &str = "https://fuchsia.dev";

/// Builds and returns human-readable Information using the provided `description`.
fn service_information(description: &String) -> Vec<bredr::Information> {
    let info = bredr::Information {
        language: Some("en".to_string()), // English
        description: Some(description.clone()),
        ..bredr::Information::EMPTY
    };
    vec![info]
}

bitfield! {
    /// The device version is represented as Binary-Coded Decimal (e.g 0x0205 -> 2.0.5).
    struct Version(u16);
    impl Debug;
    pub u8, major, set_major: 15, 8;
    pub u8, minor, set_minor: 7, 4;
    pub u8, subminor, set_subminor: 3, 0;
}

impl Clone for Version {
    fn clone(&self) -> Self {
        Version(self.0)
    }
}

impl PartialEq for Version {
    fn eq(&self, other: &Self) -> bool {
        self.0 == other.0
    }
}

impl TryFrom<&di::DeviceReleaseNumber> for Version {
    type Error = Error;

    fn try_from(src: &di::DeviceReleaseNumber) -> Result<Version, Self::Error> {
        let mut version = Version(0);
        let major = src.major.ok_or(Error::from(src))?;
        version.set_major(major);

        let minor = src.minor.filter(|&v| v < 16).ok_or(Error::from(src))?;
        version.set_minor(minor);

        let subminor = src.subminor.filter(|&v| v < 16).ok_or(Error::from(src))?;
        version.set_subminor(subminor);

        Ok(version)
    }
}

/// A validly formatted Device Identification record.
#[derive(Clone, Debug, PartialEq)]
struct DIRecord {
    vendor_id: di::VendorId,
    product_id: u16,
    version: Version,
    primary: bool,
    service_description: Option<String>,
}

impl DIRecord {
    fn vendor_id(&self) -> u16 {
        match &self.vendor_id {
            di::VendorId::BluetoothSigId(id) => *id,
            di::VendorId::UsbIfId(id) => *id,
            x => panic!("Unexpected vendor_id source: {:?}", x),
        }
    }

    fn vendor_id_source(&self) -> u16 {
        // Vendor ID source values derived from DI 1.3 Section 5.6.
        match &self.vendor_id {
            di::VendorId::BluetoothSigId(_) => 1,
            di::VendorId::UsbIfId(_) => 2,
            x => panic!("Unexpected vendor_id source: {:?}", x),
        }
    }

    fn version(&self) -> u16 {
        self.version.0
    }
}

impl TryFrom<&di::DeviceIdentificationRecord> for DIRecord {
    type Error = Error;

    fn try_from(src: &di::DeviceIdentificationRecord) -> Result<DIRecord, Self::Error> {
        let vendor_id = src.vendor_id.clone().ok_or(Error::from(src))?;
        let product_id = src.product_id.ok_or(Error::from(src))?;
        let version = src.version.as_ref().map(Version::try_from).ok_or(Error::from(src))??;
        let primary = src.primary.unwrap_or(false);
        let service_description = src.service_description.clone();

        Ok(DIRecord { vendor_id, product_id, version, primary, service_description })
    }
}

impl From<&DIRecord> for bredr::ServiceDefinition {
    fn from(src: &DIRecord) -> bredr::ServiceDefinition {
        let mut attributes = Vec::new();

        // Indicate support for DI v1.3.
        attributes.push(bredr::Attribute {
            id: ATTR_SPECIFICATION_ID,
            element: bredr::DataElement::Uint16(DEVICE_IDENTIFICATION_PROFILE_VERSION),
        });

        attributes.push(bredr::Attribute {
            id: ATTR_VENDOR_ID,
            element: bredr::DataElement::Uint16(src.vendor_id()),
        });

        attributes.push(bredr::Attribute {
            id: ATTR_PRODUCT_ID,
            element: bredr::DataElement::Uint16(src.product_id),
        });

        attributes.push(bredr::Attribute {
            id: ATTR_VERSION,
            element: bredr::DataElement::Uint16(src.version()),
        });

        attributes.push(bredr::Attribute {
            id: ATTR_PRIMARY_RECORD,
            element: bredr::DataElement::B(src.primary),
        });

        attributes.push(bredr::Attribute {
            id: ATTR_VENDOR_ID_SOURCE,
            element: bredr::DataElement::Uint16(src.vendor_id_source()),
        });

        // Specify the generic landing page for Fuchsia documentation.
        attributes.push(bredr::Attribute {
            id: ATTR_DOCUMENTATION_URL,
            element: bredr::DataElement::Url(FUCHSIA_DOCUMENTATION_URL.to_string()),
        });

        // A description of the service is optional, and must be built to indicate the right
        // language.
        let information = src.service_description.as_ref().map(service_information);

        let service_uuid = Uuid::new16(bredr::ServiceClassProfileIdentifier::PnpInformation as u16);
        bredr::ServiceDefinition {
            service_class_uuids: Some(vec![service_uuid.into()]),
            additional_attributes: Some(attributes),
            information,
            ..bredr::ServiceDefinition::EMPTY
        }
    }
}

/// A Device Identification service to be advertised over BR/EDR.
#[derive(Clone, Debug)]
pub struct DeviceIdentificationService {
    records: Vec<DIRecord>,
}

impl DeviceIdentificationService {
    pub fn size(&self) -> usize {
        self.records.len()
    }

    pub fn contains_primary(&self) -> bool {
        self.records.iter().filter(|r| r.primary).count() != 0
    }

    /// Potentially updates `records` with the primary flag. Returns true if the service was
    /// updated.
    pub fn maybe_update_primary(&mut self) -> bool {
        // Per DI v1.3 Section 5.5, if only one record is specified, then it must be marked as the
        // primary.
        let needs_primary = self.size() == 1 && !self.contains_primary();
        if needs_primary {
            self.records[0].primary = true;
        }
        needs_primary
    }

    fn validate_primary(records: &Vec<DIRecord>) -> Result<(), Error> {
        let records_with_primary = records.iter().filter(|r| r.primary).count();
        // It is invalid to specify more than one primary record. However, it is okay for no records
        // to be denoted as primary. See DI v1.3 Section 5.5.
        if records_with_primary > 1 {
            Err(Error::MultiplePrimaryRecords)
        } else {
            Ok(())
        }
    }

    /// Builds the Device Identification service from a set of FIDL records.
    pub fn from_di_records(records: &Vec<di::DeviceIdentificationRecord>) -> Result<Self, Error> {
        if records.is_empty() {
            return Err(Error::EmptyRequest);
        }
        let parsed = records.iter().map(DIRecord::try_from).collect::<Result<Vec<_>, _>>()?;
        let _ = Self::validate_primary(&parsed)?;

        Ok(Self { records: parsed })
    }
}

impl From<&DeviceIdentificationService> for Vec<bredr::ServiceDefinition> {
    fn from(src: &DeviceIdentificationService) -> Vec<bredr::ServiceDefinition> {
        src.records.iter().map(Into::into).collect()
    }
}

#[cfg(test)]
pub(crate) mod tests {
    use super::*;

    use assert_matches::assert_matches;
    use fidl_fuchsia_bluetooth_deviceid::DeviceIdentificationRecord;
    use std::convert::TryInto;

    /// Returns a device version of 2.0.6.
    fn valid_device_release_number() -> di::DeviceReleaseNumber {
        di::DeviceReleaseNumber {
            major: Some(2),
            minor: Some(0),
            subminor: Some(6),
            ..di::DeviceReleaseNumber::EMPTY
        }
    }

    /// Defines a valid DI service record.
    pub(crate) fn minimal_record(primary: bool) -> DeviceIdentificationRecord {
        let primary = if primary { Some(true) } else { None };
        DeviceIdentificationRecord {
            vendor_id: Some(di::VendorId::BluetoothSigId(9)),
            product_id: Some(1),
            version: Some(valid_device_release_number()),
            // Primary is optional.
            primary,
            // Optional `service_description` is omitted.
            ..DeviceIdentificationRecord::EMPTY
        }
    }

    #[test]
    fn di_fidl_record_with_missing_mandatory_fields_is_error() {
        assert_matches!(
            DeviceIdentificationService::from_di_records(&vec![DeviceIdentificationRecord::EMPTY]),
            Err(_)
        );

        let record = DeviceIdentificationRecord { vendor_id: None, ..minimal_record(false) };
        assert_matches!(DeviceIdentificationService::from_di_records(&vec![record]), Err(_));

        let record = DeviceIdentificationRecord { product_id: None, ..minimal_record(false) };
        assert_matches!(DeviceIdentificationService::from_di_records(&vec![record]), Err(_));

        let record = DeviceIdentificationRecord { version: None, ..minimal_record(false) };
        assert_matches!(DeviceIdentificationService::from_di_records(&vec![record]), Err(_));
    }

    #[test]
    fn no_di_records_is_error() {
        let empty = vec![];
        assert_matches!(DeviceIdentificationService::from_di_records(&empty), Err(_));
    }

    #[test]
    fn valid_di_fidl_record_can_be_parsed() {
        let valid_record = minimal_record(true);
        let mut svc = DeviceIdentificationService::from_di_records(&vec![valid_record])
            .expect("can parse record");
        assert_eq!(svc.size(), 1);
        assert!(svc.contains_primary());

        // Trying to update primary should be a no-op since this service already has one.
        assert!(!svc.maybe_update_primary());
        assert!(svc.contains_primary());

        // Conversion to `bredr` FIDL is okay.
        let bredr_record: Vec<bredr::ServiceDefinition> = (&svc).into();
        assert_eq!(bredr_record.len(), 1);
    }

    #[test]
    fn primary_field_of_singleton_service_is_updated() {
        let singleton = minimal_record(false);
        let mut svc = DeviceIdentificationService::from_di_records(&vec![singleton])
            .expect("can parse record");

        assert!(!svc.contains_primary());
        assert!(svc.maybe_update_primary());
        assert!(svc.contains_primary());
    }

    #[test]
    fn primary_field_of_multiple_service_is_not_updated() {
        let records = vec![minimal_record(false), minimal_record(false), minimal_record(false)];
        let mut svc =
            DeviceIdentificationService::from_di_records(&records).expect("can parse records");

        // It is valid to have no primary record among multiple records.
        assert!(!svc.contains_primary());
        assert!(!svc.maybe_update_primary());
        assert!(!svc.contains_primary());
    }

    #[test]
    fn multiple_primary_di_records_is_error() {
        let record1 = minimal_record(true);
        let record2 = minimal_record(true);
        let record3 = minimal_record(false);
        assert_matches!(
            DeviceIdentificationService::from_di_records(&vec![record1, record2, record3]),
            Err(_)
        );
    }

    #[test]
    fn multiple_di_records_with_no_primary_is_ok() {
        let record = DeviceIdentificationRecord {
            service_description: Some("foobar".to_string()),
            ..minimal_record(false)
        };
        assert_matches!(
            DeviceIdentificationService::from_di_records(&vec![
                minimal_record(false),
                minimal_record(false),
                record
            ]),
            Ok(_)
        );
    }

    #[test]
    fn parse_fidl_di_record_with_optional_description_into_local_success() {
        let desc = "foobar123".to_string();
        let fidl = DeviceIdentificationRecord {
            service_description: Some(desc.clone()),
            ..minimal_record(true)
        };

        let parsed: DIRecord = (&fidl).try_into().expect("valid conversion into local type");
        let expected = DIRecord {
            vendor_id: di::VendorId::BluetoothSigId(9),
            product_id: 1,
            version: Version(0x0206),
            primary: true,
            service_description: Some(desc.clone()),
        };
        assert_eq!(parsed, expected);

        // Conversion to `bredr` FIDL is okay.
        let svc = DeviceIdentificationService { records: vec![parsed] };
        let bredr_record: Vec<bredr::ServiceDefinition> = (&svc).into();
        assert_eq!(bredr_record.len(), 1);
        let expected_information = vec![bredr::Information {
            language: Some("en".to_string()), // English always specified
            description: Some(desc.clone()),
            ..bredr::Information::EMPTY
        }];
        assert_eq!(bredr_record[0].information, Some(expected_information));
    }

    #[test]
    fn parse_fidl_version_with_missing_fields_is_error() {
        let empty = di::DeviceReleaseNumber::EMPTY;
        assert_matches!(Version::try_from(&empty), Err(_));

        let missing_major =
            di::DeviceReleaseNumber { major: None, ..valid_device_release_number() };
        assert_matches!(Version::try_from(&missing_major), Err(_));

        let missing_minor =
            di::DeviceReleaseNumber { minor: None, ..valid_device_release_number() };
        assert_matches!(Version::try_from(&missing_minor), Err(_));

        let missing_subminor =
            di::DeviceReleaseNumber { subminor: None, ..valid_device_release_number() };
        assert_matches!(Version::try_from(&missing_subminor), Err(_));
    }

    #[test]
    fn parse_fidl_version_with_invalid_fields_is_error() {
        let invalid_minor = di::DeviceReleaseNumber {
            minor: Some(20), // Too large
            ..valid_device_release_number()
        };
        assert_matches!(Version::try_from(&invalid_minor), Err(_));

        let invalid_subminor = di::DeviceReleaseNumber {
            subminor: Some(23), // Too large
            ..valid_device_release_number()
        };
        assert_matches!(Version::try_from(&invalid_subminor), Err(_));
    }

    #[test]
    fn parse_version_from_fidl_success() {
        let release_number = valid_device_release_number();
        let parsed = Version::try_from(&release_number).expect("Parsing should succeed");
        let expected = Version(0x0206);
        assert_eq!(parsed, expected);
    }
}
