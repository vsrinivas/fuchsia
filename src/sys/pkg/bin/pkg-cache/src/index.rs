// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Types to track and manage indices of packages.

// TODO(fxbug.dev/77361) use this module, remove this allow.
mod dynamic;
mod package;
#[allow(dead_code)]
mod retained;

pub use dynamic::{
    enumerate_package_blobs, fulfill_meta_far_blob, load_cache_packages, DynamicIndex,
    DynamicIndexError,
};
