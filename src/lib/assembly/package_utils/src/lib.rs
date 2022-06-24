// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// 'package_utils' is a crate of utility types and fns for working with
/// fuchsia-packages.
pub use package_utils::{PackageInternalPathBuf, PackageManifestPathBuf, SourcePathBuf};

mod package_utils;
