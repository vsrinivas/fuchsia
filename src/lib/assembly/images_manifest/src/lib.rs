// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Reading and writing a manifest specifying all the images generated as part
//! of Image Assembly.

mod images_manifest;

pub use images_manifest::{Image, ImagesManifest};
