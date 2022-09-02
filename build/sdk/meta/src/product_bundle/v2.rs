// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Version 2 of the Product Bundle format.
//!
//! This format is drastically different from Version 1 in that all the contents are expected to
//! stay as implementation detail of ffx. The outputs of assembly are fed directly into the fields
//! of the Product Bundle, and the flash and emulator manifests are not constructed until the
//! Product Bundle is read by `ffx emu start` and `ffx target flash`. This makes the format
//! simpler, and more aligned with how images are assembled.
//!
//! Note on paths
//! -------------
//! PBv2 is a directory containing images and other artifacts necessary to flash, emulator, and
//! update a product. When a Product Bundle is written to disk, the paths inside _must_ all be
//! relative to the Product Bundle itself, to ensure that the directory remains portable (can be
//! moved, zipped, tarred, downloaded on another machine).

use serde::{Deserialize, Serialize};

/// Description of the data needed to set up (flash) a device.
#[derive(Clone, Debug, Deserialize, PartialEq, Serialize)]
#[serde(deny_unknown_fields)]
pub struct ProductBundleV2 {
    /// A unique name identifying this product.
    pub name: String,
}
