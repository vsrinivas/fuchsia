// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![allow(missing_debug_implementations)]

use crate::prelude_internal::*;
use crate::spinel::*;

mod fake_device_client;
mod mock_device_client;

pub use fake_device_client::*;
pub use mock_device_client::*;
