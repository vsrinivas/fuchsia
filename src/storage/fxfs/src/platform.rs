// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(target_os = "fuchsia")]
pub mod fuchsia;

#[cfg(target_os = "fuchsia")]
pub use self::fuchsia::*;

#[cfg(not(target_os = "fuchsia"))]
pub mod log {}
