// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        diagnostics::{
            Diagnostics, HashTreeOperation, IncomingManagerMethod, IncomingResetMethod,
            PinweaverMethod,
        },
        hash_tree::HashTreeError,
    },
    fidl_fuchsia_identity_credential::{CredentialError, ResetError},
    fidl_fuchsia_tpm_cr50::PinWeaverError,
    parking_lot::Mutex,
};

/// The different events that can be recorded through interactions with a `FakeDiagnostics`.
#[derive(Debug, PartialEq)]
pub enum Event {
    IncomingManagerOutcome(IncomingManagerMethod, Result<(), CredentialError>),
    IncomingResetOutcome(IncomingResetMethod, Result<(), ResetError>),
    PinweaverOutcome(PinweaverMethod, Result<(), PinWeaverError>),
    HashTreeOutcome(HashTreeOperation, Result<(), HashTreeError>),
    CredentialCount(u64),
}

/// A fake `Diagnostics` implementation useful for verifying unittest.
pub struct FakeDiagnostics {
    /// An ordered list of the events received.
    events: Mutex<Vec<Event>>,
}

impl FakeDiagnostics {
    /// Constructs a new `FakeDiagnostics`.
    pub fn new() -> Self {
        FakeDiagnostics { events: Mutex::new(Vec::new()) }
    }

    /// Panics if the supplied slice does not match the received events.
    pub fn assert_events(&self, expected: &[Event]) {
        let actual: &[Event] = &self.events.lock();
        assert_eq!(actual, expected);
    }
}

impl Diagnostics for FakeDiagnostics {
    fn incoming_manager_outcome(
        &self,
        method: IncomingManagerMethod,
        result: Result<(), CredentialError>,
    ) {
        self.events.lock().push(Event::IncomingManagerOutcome(method, result));
    }

    fn incoming_reset_outcome(&self, method: IncomingResetMethod, result: Result<(), ResetError>) {
        self.events.lock().push(Event::IncomingResetOutcome(method, result));
    }

    fn pinweaver_outcome(&self, method: PinweaverMethod, result: Result<(), PinWeaverError>) {
        self.events.lock().push(Event::PinweaverOutcome(method, result));
    }

    fn hash_tree_outcome(&self, operation: HashTreeOperation, result: Result<(), HashTreeError>) {
        self.events.lock().push(Event::HashTreeOutcome(operation, result));
    }

    fn credential_count(&self, count: u64) {
        self.events.lock().push(Event::CredentialCount(count));
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia::test]
    fn log_and_assert_incoming_manager_outcomes() {
        let diagnostics = FakeDiagnostics::new();

        diagnostics.incoming_manager_outcome(IncomingManagerMethod::AddCredential, Ok(()));
        diagnostics.incoming_manager_outcome(
            IncomingManagerMethod::RemoveCredential,
            Err(CredentialError::InvalidLabel),
        );
        diagnostics.incoming_manager_outcome(IncomingManagerMethod::CheckCredential, Ok(()));
        diagnostics.incoming_manager_outcome(
            IncomingManagerMethod::AddCredential,
            Err(CredentialError::NoFreeLabel),
        );

        diagnostics.assert_events(&[
            Event::IncomingManagerOutcome(IncomingManagerMethod::AddCredential, Ok(())),
            Event::IncomingManagerOutcome(
                IncomingManagerMethod::RemoveCredential,
                Err(CredentialError::InvalidLabel),
            ),
            Event::IncomingManagerOutcome(IncomingManagerMethod::CheckCredential, Ok(())),
            Event::IncomingManagerOutcome(
                IncomingManagerMethod::AddCredential,
                Err(CredentialError::NoFreeLabel),
            ),
        ]);
    }

    #[fuchsia::test]
    fn log_and_assert_pinweaver_outcomes() {
        let diagnostics = FakeDiagnostics::new();

        diagnostics.pinweaver_outcome(PinweaverMethod::TryAuth, Err(PinWeaverError::TreeInvalid));
        diagnostics.pinweaver_outcome(PinweaverMethod::GetLog, Ok(()));
        diagnostics.pinweaver_outcome(PinweaverMethod::ResetTree, Ok(()));

        diagnostics.assert_events(&[
            Event::PinweaverOutcome(PinweaverMethod::TryAuth, Err(PinWeaverError::TreeInvalid)),
            Event::PinweaverOutcome(PinweaverMethod::GetLog, Ok(())),
            Event::PinweaverOutcome(PinweaverMethod::ResetTree, Ok(())),
        ]);
    }

    #[fuchsia::test]
    fn log_and_assert_hash_tree_outcomes() {
        let diagnostics = FakeDiagnostics::new();

        diagnostics.hash_tree_outcome(HashTreeOperation::Load, Ok(()));
        diagnostics
            .hash_tree_outcome(HashTreeOperation::Store, Err(HashTreeError::SerializationFailed));
        diagnostics.hash_tree_outcome(HashTreeOperation::Store, Ok(()));

        diagnostics.assert_events(&[
            Event::HashTreeOutcome(HashTreeOperation::Load, Ok(())),
            Event::HashTreeOutcome(
                HashTreeOperation::Store,
                Err(HashTreeError::SerializationFailed),
            ),
            Event::HashTreeOutcome(HashTreeOperation::Store, Ok(())),
        ]);
    }

    #[fuchsia::test]
    fn log_and_assert_credential_counts() {
        let diagnostics = FakeDiagnostics::new();

        diagnostics.credential_count(3);
        diagnostics.credential_count(1);
        diagnostics.credential_count(4);

        diagnostics.assert_events(&[
            Event::CredentialCount(3),
            Event::CredentialCount(1),
            Event::CredentialCount(4),
        ]);
    }
}
