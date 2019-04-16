// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{common::Version, protocol::request::OS};

/// This is the name and version of the updater binary that is built using this crate.
///
/// This is how the updater identifies itself with the Omaha service.
///
#[derive(Clone, Debug)]
pub struct Updater {
    /// The string identifying the updater itself.  (e.g. 'Omaha', 'Fuchsia/Rust')
    pub name: String,

    /// The version of the updater itself.  (e.g '0.0.1.0')
    pub version: Version,
}

/// This struct wraps up the configuration data that an updater binary needs to supply.
///
#[derive(Clone, Debug)]
pub struct Config {
    pub updater: Updater,

    pub os: OS,
}
