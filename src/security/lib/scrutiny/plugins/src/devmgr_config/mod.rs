// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod collection;
mod collector;
mod controller;

pub use collection::{DevmgrConfigCollection, DevmgrConfigContents, DevmgrConfigError};

use {
    collector::DevmgrConfigCollector, controller::ExtractDevmgrConfigController,
    scrutiny::prelude::*, std::sync::Arc,
};

plugin!(
    DevmgrConfigPlugin,
    PluginHooks::new(
        collectors! {
            "DevmgrConfigCollector" => DevmgrConfigCollector::default(),
        },
        controllers! {
            "/devmgr/config" => ExtractDevmgrConfigController::default(),
        }
    ),
    vec![]
);
