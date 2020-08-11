// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Wrapper types for the Options table.

use {
    fidl_fuchsia_update_installer, fuchsia_inspect as inspect,
    proptest_derive::Arbitrary,
    serde::{Deserialize, Serialize},
    std::convert::TryFrom,
    thiserror::Error,
};

/// Who or what initiated the update installation.
#[derive(Clone, Debug, Copy, PartialEq, Arbitrary, Serialize, Deserialize)]
pub enum Initiator {
    /// The install was initiated by an interactive user, or the user is
    /// otherwise blocked and waiting for the result of this update.
    User,

    /// The install was initiated by a service, in the background.
    Service,
}

impl Initiator {
    fn name(&self) -> &'static str {
        match self {
            Initiator::User => "User",
            Initiator::Service => "Service",
        }
    }
}

/// Configuration options for an update attempt.
#[derive(Clone, Debug, PartialEq, Arbitrary, Serialize, Deserialize)]
pub struct Options {
    /// What initiated this update attempt.
    pub initiator: Initiator,

    /// If an update is already in progress, it's acceptable to instead attach a
    /// Monitor to that in-progress update instead of failing this request to
    /// install the update.  Setting this option to true may convert situations
    /// that would have resulted in the ALREADY_IN_PROGRESS to be treated as
    /// non-error cases. A controller, if provided, will be ignored if the
    /// running update attempt already has a controller.
    pub allow_attach_to_existing_attempt: bool,

    /// Determines if the installer should update the recovery partition if an
    /// update is available.  Defaults to true.
    pub should_write_recovery: bool,
}

impl Options {
    /// Serializes Options to a Fuchsia Inspect node.
    pub fn write_to_inspect(&self, node: &inspect::Node) {
        let Options { initiator, allow_attach_to_existing_attempt, should_write_recovery } = self;
        node.record_string("initiator", initiator.name().to_string());
        node.record_bool("allow_attach_to_existing_attempt", *allow_attach_to_existing_attempt);
        node.record_bool("should_write_recovery", *should_write_recovery);
    }
}

/// Errors for parsing fidl_update_installer_ext Options struct.
#[derive(Error, Debug, PartialEq)]
pub enum OptionsParseError {
    /// Initiator is None.
    #[error("missing initiator")]
    MissingInitiator,
}

impl TryFrom<fidl_fuchsia_update_installer::Options> for Options {
    type Error = OptionsParseError;

    fn try_from(data: fidl_fuchsia_update_installer::Options) -> Result<Self, OptionsParseError> {
        let initiator =
            data.initiator.map(|o| o.into()).ok_or(OptionsParseError::MissingInitiator)?;

        Ok(Self {
            initiator,
            allow_attach_to_existing_attempt: data
                .allow_attach_to_existing_attempt
                .unwrap_or(false),
            should_write_recovery: data.should_write_recovery.unwrap_or(true),
        })
    }
}

impl From<&Options> for fidl_fuchsia_update_installer::Options {
    fn from(options: &Options) -> Self {
        Self {
            initiator: Some(options.initiator.into()),
            allow_attach_to_existing_attempt: Some(options.allow_attach_to_existing_attempt.into()),
            should_write_recovery: Some(options.should_write_recovery.into()),
        }
    }
}

impl From<Options> for fidl_fuchsia_update_installer::Options {
    fn from(data: Options) -> Self {
        (&data).into()
    }
}

impl From<fidl_fuchsia_update_installer::Initiator> for Initiator {
    fn from(fidl_initiator: fidl_fuchsia_update_installer::Initiator) -> Self {
        match fidl_initiator {
            fidl_fuchsia_update_installer::Initiator::User => Initiator::User,
            fidl_fuchsia_update_installer::Initiator::Service => Initiator::Service,
        }
    }
}

impl From<Initiator> for fidl_fuchsia_update_installer::Initiator {
    fn from(initiator: Initiator) -> Self {
        match initiator {
            Initiator::User => fidl_fuchsia_update_installer::Initiator::User,
            Initiator::Service => fidl_fuchsia_update_installer::Initiator::Service,
        }
    }
}

#[cfg(test)]
mod tests {

    use {
        super::*,
        proptest::prelude::*,
        std::convert::{TryFrom, TryInto},
    };

    proptest! {
        #[test]
        /// Verifies that converting any instance of Options to fidl_fuchsia_update_installer::Options
        /// and back to Options produces exactly the same options that we started with.
        fn options_roundtrips_through_fidl(options: Options) {
            let as_fidl: fidl_fuchsia_update_installer::Options = options.clone().into();
            prop_assert_eq!(as_fidl.try_into(), Ok(options));
        }

        #[test]
        /// Verifies that a fidl_fuchsia_update_installer::Options without an Initiator raises an error.
        fn fidl_options_sans_initiator_error(options: Options) {
            let mut as_fidl: fidl_fuchsia_update_installer::Options = options.clone().into();
            as_fidl.initiator = None;
            prop_assert_eq!(Options::try_from(as_fidl), Err(OptionsParseError::MissingInitiator));
        }
    }
}
