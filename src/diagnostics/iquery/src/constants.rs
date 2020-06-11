// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    fuchsia_zircon::{Duration, DurationNum},
    lazy_static::lazy_static,
};

lazy_static! {
    pub static ref IQUERY_TIMEOUT: Duration = 5_i64.seconds();
}
