// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{error::RightsRoutingError, walk_state::WalkStateUnit},
    fidl_fuchsia_io::{self as fio},
    lazy_static::lazy_static,
    std::convert::From,
};

lazy_static! {
    // TODO: const initialization of bitflag types with bitwise-or is not supported in FIDL. Change
    // when supported.
    /// All rights corresponding to r*.
    pub static ref READ_RIGHTS: fio::Operations =
        fio::Operations::CONNECT
        | fio::Operations::ENUMERATE
        | fio::Operations::TRAVERSE
        | fio::Operations::READ_BYTES
        | fio::Operations::GET_ATTRIBUTES;
    /// All rights corresponding to w*.
    pub static ref WRITE_RIGHTS: fio::Operations =
        fio::Operations::CONNECT
        | fio::Operations::ENUMERATE
        | fio::Operations::TRAVERSE
        | fio::Operations::WRITE_BYTES
        | fio::Operations::MODIFY_DIRECTORY
        | fio::Operations::UPDATE_ATTRIBUTES;

    /// All the fio rights required to represent fio::OPEN_RIGHT_READABLE.
    static ref LEGACY_READABLE_RIGHTS: fio::Operations =
        fio::Operations::READ_BYTES
        | fio::Operations::GET_ATTRIBUTES
        | fio::Operations::TRAVERSE
        | fio::Operations::ENUMERATE;
    /// All the fio rights required to represent fio::OPEN_RIGHT_WRITABLE.
    static ref LEGACY_WRITABLE_RIGHTS: fio::Operations =
        fio::Operations::WRITE_BYTES
        | fio::Operations::UPDATE_ATTRIBUTES
        | fio::Operations::MODIFY_DIRECTORY;
    /// All the fio rights required to represent fio::OPEN_RIGHT_EXECUTABLE.
    static ref LEGACY_EXECUTABLE_RIGHTS: fio::Operations = fio::Operations::EXECUTE;
}

/// Opaque rights type to define new traits like PartialOrd on.
#[derive(Debug, PartialEq, Eq, Clone)]
pub struct Rights(fio::Operations);

impl Rights {
    /// Converts new fuchsia.io directory rights to legacy fuchsia.io compatible rights. This will
    /// be remove once new rights are supported by component manager.
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
        if rights.contains(fio::Operations::EXECUTE) {
            flags |= fio::OPEN_RIGHT_EXECUTABLE;
        }
        // Since there is no direct translation for connect in CV1 we must explicitly define it
        // here as both flags.
        //
        // TODO(fxbug.dev/60673): Is this correct? ReadBytes | Connect seems like it should translate to
        // READABLE | WRITABLE, not empty rights.
        if flags == 0 && rights.contains(fio::Operations::CONNECT) {
            flags |= fio::OPEN_RIGHT_READABLE | fio::OPEN_RIGHT_WRITABLE;
        }
        flags
    }
}

/// Allows creating rights from fio::Operations.
impl From<fio::Operations> for Rights {
    fn from(operations: fio::Operations) -> Self {
        Rights(operations)
    }
}

impl WalkStateUnit for Rights {
    type Error = RightsRoutingError;

    /// Ensures the next walk state of rights satisfies a monotonic increasing sequence. Used to
    /// verify the expectation that no right requested from a use, offer, or expose is missing as
    /// capability routing walks from the capability's consumer to its provider.
    fn validate_next(&self, next_rights: &Rights) -> Result<(), Self::Error> {
        if next_rights.0.contains(self.0) {
            Ok(())
        } else {
            Err(RightsRoutingError::Invalid)
        }
    }

    fn finalize_error() -> Self::Error {
        RightsRoutingError::MissingRightsSource
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use assert_matches::assert_matches;

    #[test]
    fn validate_next() {
        assert_matches!(
            Rights::from(fio::Operations::empty())
                .validate_next(&Rights::from(*LEGACY_READABLE_RIGHTS)),
            Ok(())
        );
        assert_matches!(
            Rights::from(fio::Operations::READ_BYTES | fio::Operations::GET_ATTRIBUTES)
                .validate_next(&Rights::from(*LEGACY_READABLE_RIGHTS)),
            Ok(())
        );
        assert_matches!(
            Rights::from(Rights::from(*LEGACY_READABLE_RIGHTS)).validate_next(&Rights::from(
                fio::Operations::READ_BYTES | fio::Operations::GET_ATTRIBUTES
            )),
            Err(RightsRoutingError::Invalid)
        );
        assert_matches!(
            Rights::from(fio::Operations::WRITE_BYTES).validate_next(&Rights::from(
                fio::Operations::READ_BYTES | fio::Operations::GET_ATTRIBUTES
            )),
            Err(RightsRoutingError::Invalid)
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
            Rights::from(fio::Operations::READ_BYTES).into_legacy(),
            fio::OPEN_RIGHT_READABLE
        );
        assert_eq!(
            Rights::from(fio::Operations::GET_ATTRIBUTES).into_legacy(),
            fio::OPEN_RIGHT_READABLE
        );
        assert_eq!(Rights::from(fio::Operations::TRAVERSE).into_legacy(), fio::OPEN_RIGHT_READABLE);
        assert_eq!(
            Rights::from(fio::Operations::ENUMERATE).into_legacy(),
            fio::OPEN_RIGHT_READABLE
        );
        assert_eq!(
            Rights::from(fio::Operations::WRITE_BYTES).into_legacy(),
            fio::OPEN_RIGHT_WRITABLE
        );
        assert_eq!(
            Rights::from(fio::Operations::UPDATE_ATTRIBUTES).into_legacy(),
            fio::OPEN_RIGHT_WRITABLE
        );
        assert_eq!(
            Rights::from(fio::Operations::MODIFY_DIRECTORY).into_legacy(),
            fio::OPEN_RIGHT_WRITABLE
        );
    }
}
