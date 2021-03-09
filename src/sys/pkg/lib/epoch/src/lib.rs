// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![deny(missing_docs)]

//! Wrapper around (de)serializing an epoch.json file. Although the underlying implementation is
//! fairly straightforward, we extract this into a library so that the library serves as the "source
//! of truth" for all valid epoch.json formats. Currently, there is only one format documented in
//! [RFC-0071](https://fuchsia.dev/fuchsia-src/contribute/governance/rfcs/0071_ota_backstop).
//! If we add a new format (e.g. change the version field) in the future, this library will ensure
//! that all clients are aligned on format changes.

mod epoch;

pub use crate::epoch::EpochFile;
