// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod error;
mod extent;
pub mod format;
pub mod properties;
mod utils;
pub use error::Error;
pub use format::{DataKindInfo, ExtentInfo, ExtentKindInfo, EXTENT_KIND_UNMAPPED};

pub use properties::{DataKind, ExtentKind, ExtentProperties};
