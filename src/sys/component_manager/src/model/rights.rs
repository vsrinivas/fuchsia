// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fidl_fuchsia_io2::{self as fio2},
    lazy_static::lazy_static,
};

// TODO(https://fxbug.dev/71901): remove aliases once the routing lib has a stable API.
pub type Rights = ::routing::rights::Rights;

lazy_static! {
    /// All rights corresponding to r*.
    pub static ref READ_RIGHTS: fio2::Operations = *::routing::rights::READ_RIGHTS;
    /// All rights corresponding to w*.
    pub static ref WRITE_RIGHTS: fio2::Operations = *::routing::rights::WRITE_RIGHTS;
}
