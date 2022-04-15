// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{Diagnostics, IncomingMethod},
    fidl_fuchsia_identity_credential::CredentialError,
    parking_lot::Mutex,
};

/// A fake `Diagnostics` implementation useful for verifying unittest.
pub struct FakeDiagnostics {
    /// An ordered list of the incoming RPC events received.
    incoming_outcomes: Mutex<Vec<(IncomingMethod, Result<(), CredentialError>)>>,
}

impl FakeDiagnostics {
    /// Constructs a new `FakeDiagnostics`.
    pub fn new() -> Self {
        FakeDiagnostics { incoming_outcomes: Mutex::new(Vec::new()) }
    }

    /// Panics if the supplied slice does not match the received incoming_outcomes.
    pub fn assert_incoming_outcomes(
        &self,
        expected: &[(IncomingMethod, Result<(), CredentialError>)],
    ) {
        let actual: &[(IncomingMethod, Result<(), CredentialError>)] =
            &*self.incoming_outcomes.lock();
        assert_eq!(actual, expected);
    }
}

impl Diagnostics for FakeDiagnostics {
    fn incoming_outcome(&self, method: IncomingMethod, result: Result<(), CredentialError>) {
        self.incoming_outcomes.lock().push((method, result));
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia::test]
    fn log_and_assert_incoming_outcomes() {
        let diagnostics = FakeDiagnostics::new();

        diagnostics.incoming_outcome(IncomingMethod::AddCredential, Ok(()));
        diagnostics
            .incoming_outcome(IncomingMethod::RemoveCredential, Err(CredentialError::InvalidLabel));
        diagnostics.incoming_outcome(IncomingMethod::CheckCredential, Ok(()));
        diagnostics
            .incoming_outcome(IncomingMethod::AddCredential, Err(CredentialError::NoFreeLabel));

        diagnostics.assert_incoming_outcomes(&[
            (IncomingMethod::AddCredential, Ok(())),
            (IncomingMethod::RemoveCredential, Err(CredentialError::InvalidLabel)),
            (IncomingMethod::CheckCredential, Ok(())),
            (IncomingMethod::AddCredential, Err(CredentialError::NoFreeLabel)),
        ]);
    }
}
