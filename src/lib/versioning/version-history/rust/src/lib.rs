// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::{
    array::TryFromSliceError,
    convert::{TryFrom, TryInto},
    fmt,
};

/// VERSION_HISTORY is an array of all the known SDK versions.  It is guaranteed
/// (at compile-time) by the proc_macro to be non-empty.
pub const VERSION_HISTORY: &[Version] = &version_history_macro::declare_version_history!();

/// LATEST_VERSION is the latest known SDK version.
pub const LATEST_VERSION: &Version = &version_history_macro::latest_sdk_version!();

/// An `AbiRevision` represents the ABI revision of a Fuchsia Package.
/// https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0135_package_abi_revision?#design
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Copy, Clone)]
pub struct AbiRevision(pub u64);

impl AbiRevision {
    pub const PATH: &'static str = "meta/fuchsia.abi/abi-revision";

    pub fn new(u: u64) -> AbiRevision {
        AbiRevision(u)
    }

    /// Parse the ABI revision from little-endian bytes.
    pub fn from_bytes(b: [u8; 8]) -> Self {
        AbiRevision(u64::from_le_bytes(b))
    }

    /// Encode the ABI revision into little-endian bytes.
    pub fn as_bytes(&self) -> [u8; 8] {
        self.0.to_le_bytes()
    }
}

impl fmt::Display for AbiRevision {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{:x}", self.0)
    }
}

impl From<u64> for AbiRevision {
    fn from(abi_revision: u64) -> AbiRevision {
        AbiRevision(abi_revision)
    }
}

impl From<AbiRevision> for u64 {
    fn from(abi_revision: AbiRevision) -> u64 {
        abi_revision.0
    }
}

impl From<[u8; 8]> for AbiRevision {
    fn from(abi_revision: [u8; 8]) -> AbiRevision {
        AbiRevision::from_bytes(abi_revision)
    }
}

impl TryFrom<&[u8]> for AbiRevision {
    type Error = TryFromSliceError;

    fn try_from(abi_revision: &[u8]) -> Result<AbiRevision, Self::Error> {
        let abi_revision: [u8; 8] = abi_revision.try_into()?;
        Ok(AbiRevision::from_bytes(abi_revision))
    }
}

impl std::ops::Deref for AbiRevision {
    type Target = u64;

    fn deref(&self) -> &Self::Target {
        &self.0
    }
}

/// Version is a mapping between the supported API level and the ABI revisions.
///
/// See https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0002_platform_versioning for more
/// details.
#[derive(Debug, PartialEq, Eq, PartialOrd, Ord, Hash, Clone)]
pub struct Version {
    /// The API level denotes a set of APIs available when building an application for a given
    /// release of the FUCHSIA IDK.
    pub api_level: u64,

    /// The ABI revision denotes the semantics of the Fuchsia System Interface that an application
    /// expects the platform to provide.
    pub abi_revision: AbiRevision,
}

/// Returns true if the given abi_revision is listed in the VERSION_HISTORY of
/// known SDK versions.
pub fn is_valid_abi_revision(abi_revision: AbiRevision) -> bool {
    VERSION_HISTORY.iter().any(|v| v.abi_revision == abi_revision)
}

#[cfg(test)]
mod tests {
    use super::*;
    use proptest::prelude::*;

    // Helper to convert from the shared crate's Version struct to this crate's
    // struct.
    fn version_history_from_shared() -> Vec<Version> {
        version_history_shared::version_history()
            .unwrap()
            .into_iter()
            .map(|v| Version {
                api_level: v.api_level,
                abi_revision: AbiRevision::new(v.abi_revision.value),
            })
            .collect::<Vec<_>>()
    }

    #[test]
    fn test_proc_macro_worked() {
        let expected = version_history_from_shared();
        assert_eq!(expected, VERSION_HISTORY);
    }

    #[test]
    fn test_latest_version_proc_macro() {
        let shared_versions = version_history_from_shared();
        let expected = shared_versions.last().expect("version_history_shared was empty");
        assert_eq!(expected, &version_history_macro::latest_sdk_version!());
    }

    #[test]
    fn test_valid_abi_revision() {
        for version in VERSION_HISTORY {
            assert!(is_valid_abi_revision(version.abi_revision));
        }
    }

    // To ensure this test doesn't flake, proptest is used to generate u64
    // values which are not in the current VERSION_HISTORY list.
    proptest! {
        #[test]
        fn test_invalid_abi_revision(u in any::<u64>().prop_filter("using u64 that isn't in VERSION_HISTORY", |u|
            // The randomly chosen 'abi_revision' must not equal any of the
            // abi_revisions in the VERSION_HISTORY list.
            VERSION_HISTORY.iter().all(|v| v.abi_revision != AbiRevision::new(*u))
        )) {
            assert!(!is_valid_abi_revision(AbiRevision::new(u)))
        }
    }
}
