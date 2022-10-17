// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use std::time::Duration;

pub const IQUERY_TIMEOUT_SECS: u64 = 5;
pub const IQUERY_TIMEOUT: Duration = Duration::from_secs(IQUERY_TIMEOUT_SECS);
