// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![warn(missing_docs)]

mod asset;
mod cache;
mod collection;

pub use {
    asset::AssetId,
    collection::{AssetCollection, AssetCollectionBuilder, AssetCollectionError},
};
