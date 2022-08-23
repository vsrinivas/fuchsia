// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Library for constructing driver manifests and the driver-manager-base-config package.

mod driver_manifest;

pub use driver_manifest::DriverManifestBuilder;
