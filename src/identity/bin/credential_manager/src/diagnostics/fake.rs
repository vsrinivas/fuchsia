// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{Diagnostics, RpcMethod},
    fidl_fuchsia_identity_credential::CredentialError,
    parking_lot::Mutex,
};

/// A fake `Diagnostics` implementation useful for verifying unittest.
pub struct FakeDiagnostics {
    /// An ordered list of the RPC events received.
    rpc_outcomes: Mutex<Vec<(RpcMethod, Result<(), CredentialError>)>>,
}

impl FakeDiagnostics {
    /// Constructs a new `FakeDiagnostics`.
    pub fn new() -> Self {
        FakeDiagnostics { rpc_outcomes: Mutex::new(Vec::new()) }
    }

    /// Panics if the supplied slice does not match the received rpc_outcomes.
    pub fn assert_rpc_outcomes(&self, expected: &[(RpcMethod, Result<(), CredentialError>)]) {
        let actual: &[(RpcMethod, Result<(), CredentialError>)] = &*self.rpc_outcomes.lock();
        assert_eq!(actual, expected);
    }
}

impl Diagnostics for FakeDiagnostics {
    fn rpc_outcome(&self, method: RpcMethod, result: Result<(), CredentialError>) {
        self.rpc_outcomes.lock().push((method, result));
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[fuchsia::test]
    fn log_and_assert_rpc_outcomes() {
        let diagnostics = FakeDiagnostics::new();

        diagnostics.rpc_outcome(RpcMethod::AddCredential, Ok(()));
        diagnostics.rpc_outcome(RpcMethod::RemoveCredential, Err(CredentialError::InvalidLabel));
        diagnostics.rpc_outcome(RpcMethod::CheckCredential, Ok(()));
        diagnostics.rpc_outcome(RpcMethod::AddCredential, Err(CredentialError::NoFreeLabel));

        diagnostics.assert_rpc_outcomes(&[
            (RpcMethod::AddCredential, Ok(())),
            (RpcMethod::RemoveCredential, Err(CredentialError::InvalidLabel)),
            (RpcMethod::CheckCredential, Ok(())),
            (RpcMethod::AddCredential, Err(CredentialError::NoFreeLabel)),
        ]);
    }
}
