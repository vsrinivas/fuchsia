// Copyright 2019 The Fuchsia Authors. All right reserved.
// Use of this source code is goverend by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::model::walk_state::WalkStateUnit,
    fidl_fuchsia_io::{self as fio},
    fidl_fuchsia_io2::{self as fio2},
    fuchsia_zircon as zx,
    lazy_static::lazy_static,
    std::convert::From,
    thiserror::Error,
};

lazy_static! {
    // TODO: const initialization of bitflag types with bitwise-or is not supported in FIDL. Change
    // when supported.
    /// All rights corresponding to r*.
    pub static ref READ_RIGHTS: fio2::Operations =
        fio2::Operations::Connect
        | fio2::Operations::Enumerate
        | fio2::Operations::Traverse
        | fio2::Operations::ReadBytes
        | fio2::Operations::GetAttributes;
    /// All rights corresponding to w*.
    pub static ref WRITE_RIGHTS: fio2::Operations =
        fio2::Operations::Connect
        | fio2::Operations::Enumerate
        | fio2::Operations::Traverse
        | fio2::Operations::WriteBytes
        | fio2::Operations::ModifyDirectory
        | fio2::Operations::UpdateAttributes;

    /// All the fio2 rights required to represent fio::OPEN_RIGHT_READABLE.
    static ref LEGACY_READABLE_RIGHTS: fio2::Operations =
        fio2::Operations::ReadBytes
        | fio2::Operations::GetAttributes
        | fio2::Operations::Traverse
        | fio2::Operations::Enumerate;
    /// All the fio2 rights required to represent fio::OPEN_RIGHT_WRITABLE.
    static ref LEGACY_WRITABLE_RIGHTS: fio2::Operations =
        fio2::Operations::WriteBytes
        | fio2::Operations::UpdateAttributes
        | fio2::Operations::ModifyDirectory;
    /// All the fio2 rights required to represent fio::OPEN_RIGHT_EXECUTABLE.
    static ref LEGACY_EXECUTABLE_RIGHTS: fio2::Operations = fio2::Operations::Execute;
}

/// Opaque rights type to define new traits like PartialOrd on.
#[derive(Debug, PartialEq, Eq, Clone)]
pub struct Rights(fio2::Operations);

impl Rights {
    /// Converts fuchsia.io2 directory rights to fuchsia.io compatible rights. This will be removed
    /// once fuchsia.io2 is supported by component manager.
    pub fn into_legacy(&self) -> u32 {
        let mut flags: u32 = 0;
        let rights = self.0;
        // The `intersects` below is intentional. The translation from io2 to io rights is lossy
        // in the sense that a single io2 right may require an io right with coarser permissions.
        if rights.intersects(*LEGACY_READABLE_RIGHTS) {
            flags |= fio::OPEN_RIGHT_READABLE;
        }
        if rights.intersects(*LEGACY_WRITABLE_RIGHTS) {
            flags |= fio::OPEN_RIGHT_WRITABLE;
        }
        if rights.contains(fio2::Operations::Execute) {
            flags |= fio::OPEN_RIGHT_EXECUTABLE;
        }
        if rights.contains(fio2::Operations::Admin) {
            flags |= fio::OPEN_RIGHT_ADMIN;
        }
        // Since there is no direct translation for connect in CV1 we must explicitly define it
        // here as both flags.
        //
        // TODO(fxbug.dev/60673): Is this correct? ReadBytes | Connect seems like it should translate to
        // READABLE | WRITABLE, not empty rights.
        if flags == 0 && rights.contains(fio2::Operations::Connect) {
            flags |= fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE;
        }
        flags
    }
}

/// Allows creating rights from fio2::Operations.
impl From<fio2::Operations> for Rights {
    fn from(operations: fio2::Operations) -> Self {
        Rights(operations)
    }
}

#[derive(Debug, Error, Clone)]
pub enum RightsError {
    #[error("Requested rights greater than provided rights")]
    Invalid,

    #[error("Directory routes must end at source with a rights declaration")]
    InvalidFinalize,
}

impl RightsError {
    /// Convert this error into its approximate `zx::Status` equivalent.
    pub fn as_zx_status(&self) -> zx::Status {
        zx::Status::UNAVAILABLE
    }
}

impl WalkStateUnit for Rights {
    type Error = RightsError;

    /// Ensures the next walk state of rights satisfies a monotonic increasing sequence. Used to
    /// verify the expectation that no right requested from a use, offer, or expose is missing as
    /// capability routing walks from the capability's consumer to its provider.
    fn validate_next(&self, next_rights: &Rights) -> Result<(), Self::Error> {
        if next_rights.0.contains(self.0) {
            Ok(())
        } else {
            Err(RightsError::Invalid)
        }
    }

    fn finalize_error() -> Self::Error {
        RightsError::InvalidFinalize
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use matches::assert_matches;

    #[test]
    fn validate_next() {
        assert_matches!(
            Rights::from(fio2::Operations::empty())
                .validate_next(&Rights::from(*LEGACY_READABLE_RIGHTS)),
            Ok(())
        );
        assert_matches!(
            Rights::from(fio2::Operations::ReadBytes | fio2::Operations::GetAttributes)
                .validate_next(&Rights::from(*LEGACY_READABLE_RIGHTS)),
            Ok(())
        );
        assert_matches!(
            Rights::from(Rights::from(*LEGACY_READABLE_RIGHTS)).validate_next(&Rights::from(
                fio2::Operations::ReadBytes | fio2::Operations::GetAttributes
            )),
            Err(RightsError::Invalid)
        );
        assert_matches!(
            Rights::from(fio2::Operations::WriteBytes).validate_next(&Rights::from(
                fio2::Operations::ReadBytes | fio2::Operations::GetAttributes
            )),
            Err(RightsError::Invalid)
        );
    }

    #[test]
    fn into_legacy() {
        assert_eq!(Rights::from(*LEGACY_READABLE_RIGHTS).into_legacy(), fio::OPEN_RIGHT_READABLE);
        assert_eq!(Rights::from(*LEGACY_WRITABLE_RIGHTS).into_legacy(), fio::OPEN_RIGHT_WRITABLE);
        assert_eq!(
            Rights::from(*LEGACY_EXECUTABLE_RIGHTS).into_legacy(),
            fio::OPEN_RIGHT_EXECUTABLE
        );
        assert_eq!(
            Rights::from(*LEGACY_READABLE_RIGHTS | *LEGACY_WRITABLE_RIGHTS).into_legacy(),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE
        );
        assert_eq!(
            Rights::from(
                *LEGACY_READABLE_RIGHTS | *LEGACY_WRITABLE_RIGHTS | *LEGACY_EXECUTABLE_RIGHTS
            )
            .into_legacy(),
            fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE | fio::OPEN_RIGHT_EXECUTABLE
        );
        assert_eq!(
            Rights::from(fio2::Operations::ReadBytes).into_legacy(),
            fio::OPEN_RIGHT_READABLE
        );
        assert_eq!(
            Rights::from(fio2::Operations::GetAttributes).into_legacy(),
            fio::OPEN_RIGHT_READABLE
        );
        assert_eq!(
            Rights::from(fio2::Operations::Traverse).into_legacy(),
            fio::OPEN_RIGHT_READABLE
        );
        assert_eq!(
            Rights::from(fio2::Operations::Enumerate).into_legacy(),
            fio::OPEN_RIGHT_READABLE
        );
        assert_eq!(
            Rights::from(fio2::Operations::WriteBytes).into_legacy(),
            fio::OPEN_RIGHT_WRITABLE
        );
        assert_eq!(
            Rights::from(fio2::Operations::UpdateAttributes).into_legacy(),
            fio::OPEN_RIGHT_WRITABLE
        );
        assert_eq!(
            Rights::from(fio2::Operations::ModifyDirectory).into_legacy(),
            fio::OPEN_RIGHT_WRITABLE
        );
    }
}
