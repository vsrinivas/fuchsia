// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Types that are shared across more than one Account Management crate.
#![deny(warnings)]
#![deny(missing_docs)]
#![feature(async_await, await_macro)]

extern crate serde;

/// More ergonomic wrapper types for FIDL auth states.
mod auth_state;
/// An error type for problems encountered in the account manager and account handler.
mod error;
/// More ergonomic wrapper types for FIDL account and persona identifiers.
mod identifiers;

pub use crate::auth_state::{AccountAuthState, FidlAccountAuthState};
pub use crate::error::{AccountManagerError, ResultExt};
pub use crate::identifiers::{
    FidlGlobalAccountId, FidlLocalAccountId, FidlLocalPersonaId, GlobalAccountId, LocalAccountId,
    LocalPersonaId,
};
