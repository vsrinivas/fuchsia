// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#[cfg(target_os = "fuchsia")]
mod zircon;

#[cfg(target_os = "fuchsia")]
pub use zircon::*;

#[cfg(not(target_os = "fuchsia"))]
mod emulated;

#[cfg(not(target_os = "fuchsia"))]
pub use emulated::*;
