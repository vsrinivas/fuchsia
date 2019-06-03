// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! `fidl_fuchsia_amber_ext` contains wrapper types around the auto-generated `fidl_fuchsia_amber`
//! bindings.

mod types;
pub use crate::types::{
    BlobEncryptionKey, SourceConfig, SourceConfigBuilder, StatusConfig, TransportConfig,
};
