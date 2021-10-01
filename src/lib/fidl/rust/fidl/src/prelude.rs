// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! A "prelude" of common FIDL traits, to be used like `use fidl::prelude::*;`.
//! This module re-exports traits using the `as _` syntax, so the glob import
//! only brings traits into scope for resolving methods and constants. It does
//! not import the trait names themselves.

pub use crate::client::QueryResponseFut as _;

pub use crate::endpoints::{
    ControlHandle as _, DiscoverableProtocolMarker as _, ProtocolMarker as _, Proxy as _,
    RequestStream as _, Responder as _,
};

#[cfg(target_os = "fuchsia")]
pub use crate::endpoints::{
    MemberOpener as _, ServiceMarker as _, ServiceProxy as _, ServiceRequest as _,
};

pub use crate::handle::AsHandleRef as _;
