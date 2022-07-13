// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Types to track and manage indices of packages.

mod dynamic;
mod package;
mod retained;

pub use package::{
    fulfill_meta_far_blob, load_cache_packages, set_retained_index, CompleteInstallError,
    FulfillMetaFarError, PackageIndex,
};

#[cfg(test)]
pub use package::register_dynamic_package;
