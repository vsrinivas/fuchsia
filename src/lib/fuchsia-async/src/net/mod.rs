// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(target_os = "fuchsia")]
mod fuchsia;

#[cfg(target_os = "fuchsia")]
pub use fuchsia::*;

#[cfg(not(target_os = "fuchsia"))]
mod portable;

#[cfg(not(target_os = "fuchsia"))]
pub use portable::*;
