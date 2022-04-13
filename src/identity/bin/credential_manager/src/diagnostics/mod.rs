// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(test)]
mod fake;
mod inspect;

#[cfg(test)]
pub use self::fake::FakeDiagnostics;
pub use self::inspect::{InspectDiagnostics, INSPECTOR};

use fidl_fuchsia_identity_credential::CredentialError;

/// The different RPC methods that may be called on a CredentialManager.
#[derive(Debug, PartialEq)]
pub enum RpcMethod {
    AddCredential,
    RemoveCredential,
    CheckCredential,
}

/// A standard interface for systems that record CredentialManger events for diagnostics purposes.
pub trait Diagnostics {
    /// Records the result of a CredentialManager RPC.
    fn rpc_outcome(&self, method: RpcMethod, result: Result<(), CredentialError>);
}
