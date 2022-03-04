// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::prelude_internal::*;

/// Route Preference, as defined in [RFC4191 Section 2.1][1].
///
/// [1]: https://datatracker.ietf.org/doc/html/rfc4191#section-2.1
#[derive(Debug, Copy, Clone, Eq, Ord, PartialOrd, PartialEq, num_derive::FromPrimitive)]
#[allow(missing_docs)]
pub enum RoutePreference {
    Low = OT_ROUTE_PREFERENCE_LOW as isize,
    Medium = OT_ROUTE_PREFERENCE_MED as isize,
    High = OT_ROUTE_PREFERENCE_HIGH as isize,
}
