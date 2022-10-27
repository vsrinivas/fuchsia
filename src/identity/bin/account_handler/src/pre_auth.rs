// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This file contains a data type for pre-authentication state, which is
//! mutable per-account data that is readable when an account is locked.

use {
    account_common::AccountId,
    fidl_fuchsia_identity_account::Error as ApiError,
    serde::{Deserialize, Serialize},
    tracing::warn,
};

/// The current (latest) version of the Pre-auth state
const PRE_AUTH_STATE_VERSION: u32 = 1;

/// The pre-authentication state for an account.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
pub struct State {
    /// Pre-Auth State Version
    ///
    /// We may modify the struct or its methods based on the version.
    /// We only support a single version at the moment and don't enforce any
    /// checks.
    version: u32,

    /// The Account ID
    account_id: AccountId,

    /// The enrollment state for this account
    pub enrollment_state: EnrollmentState,
}

impl TryFrom<Vec<u8>> for State {
    type Error = ApiError;
    fn try_from(data: Vec<u8>) -> Result<Self, Self::Error> {
        bincode::deserialize(&data).map_err(|err| {
            warn!("Failed to deserialize Pre-auth state: {:?}", err);
            ApiError::InvalidRequest
        })
    }
}

impl<'a> TryInto<Vec<u8>> for &'a State {
    type Error = ApiError;

    fn try_into(self) -> Result<Vec<u8>, Self::Error> {
        bincode::serialize(self).map_err(|err| {
            warn!("Failed to serialize Pre-auth state: {:?}", err);
            ApiError::Internal
        })
    }
}

impl State {
    pub fn new(account_id: AccountId, enrollment_state: EnrollmentState) -> Self {
        Self { version: PRE_AUTH_STATE_VERSION, account_id, enrollment_state }
    }
}

/// State of the Enrollment associated with a system account.
#[derive(Clone, Debug, PartialEq, Deserialize, Serialize)]
pub enum EnrollmentState {
    /// No authentication mechanism enrollments.
    NoEnrollments,

    /// A single enrollment of an authentication mechanism,
    /// containig the ID of the authentication mechanism and
    /// the enrollment data for it.
    SingleEnrollment { auth_mechanism_id: String, data: Vec<u8> },
}

#[cfg(test)]
mod tests {
    use super::*;
    use fuchsia_async as fasync;
    use lazy_static::lazy_static;

    lazy_static! {
        static ref TEST_ENROLLMENT_STATE: EnrollmentState = EnrollmentState::SingleEnrollment {
            auth_mechanism_id: String::from("test_id"),
            data: vec![1, 2, 3],
        };
        static ref TEST_ACCOUNT_ID_1: AccountId = AccountId::new(1);
        static ref TEST_STATE: State =
            State::new(TEST_ACCOUNT_ID_1.clone(), TEST_ENROLLMENT_STATE.clone(),);
        static ref TEST_STATE_BYTES: Vec<u8> = vec![
            1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 7, 0, 0, 0, 0, 0, 0, 0, 116, 101, 115,
            116, 95, 105, 100, 3, 0, 0, 0, 0, 0, 0, 0, 1, 2, 3
        ];
    }

    #[fasync::run_until_stalled(test)]
    async fn convert_to_bytes_and_back() {
        let state_bytes: Vec<u8> = (&*TEST_STATE).try_into().unwrap();
        let state_from_bytes: State = state_bytes.try_into().unwrap();
        assert_eq!(&*TEST_STATE, &state_from_bytes);
    }

    #[fasync::run_until_stalled(test)]
    async fn convert_from_empty_bytes() {
        assert_eq!(State::try_from(vec![]), Err(ApiError::InvalidRequest));
    }

    #[fasync::run_until_stalled(test)]
    async fn check_golden() {
        let state_bytes: Vec<u8> = (&*TEST_STATE).try_into().unwrap();
        assert_eq!(state_bytes, *TEST_STATE_BYTES);
    }
}
