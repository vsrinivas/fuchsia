// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::diagnostics::{Diagnostics, RpcMethod},
    fidl_fuchsia_identity_credential::CredentialError,
    fuchsia_inspect::{Inspector, Node},
    lazy_static::lazy_static,
};

lazy_static! {
    pub static ref INSPECTOR: Inspector = Inspector::new();
}

/// The complete set of CredentialManager information exported through Inspect.
pub struct InspectDiagnostics {}

impl InspectDiagnostics {
    /// Construct a new `InspectDiagnostics` exporting at the root of the default Inspector.
    pub fn new_at_root() -> Self {
        Self::new(&*INSPECTOR.root())
    }

    /// Construct a new `InspectDiagnostics` exporting at the supplied `Node`.
    fn new(_node: &Node) -> Self {
        Self {}
    }
}

impl Diagnostics for InspectDiagnostics {
    fn rpc_outcome(&self, _method: RpcMethod, _result: Result<(), CredentialError>) {
        // TODO(jsankey): Implement this method
    }
}
