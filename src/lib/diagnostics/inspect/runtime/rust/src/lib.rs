// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! # Inspect Runtime
//!
//! This library contains the necessary functions to serve inspect from a component.

use fuchsia_component::server::{ServiceFs, ServiceObjTrait};
use fuchsia_inspect::{Error, Inspector};

/// Spawns a server for handling `fuchsia.inspect.Tree` requests in the outgoing diagnostics
/// directory.
pub fn serve<'a, ServiceObjTy: ServiceObjTrait>(
    inspector: &Inspector,
    service_fs: &mut ServiceFs<ServiceObjTy>,
) -> Result<(), Error> {
    inspector.serve(service_fs)
}
