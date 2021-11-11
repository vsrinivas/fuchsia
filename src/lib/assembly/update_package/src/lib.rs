// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Library for constructing the Update Package.

mod update_package;
mod update_package2;

pub use update_package::UpdatePackageBuilder;
pub use update_package2::{Slot, UpdatePackageBuilder2};
