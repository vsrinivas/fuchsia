// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod collection;
mod collector;
mod controller;

pub use collection::{BootFsCollection, CmdlineCollection};

use {
    collector::{BootFsCollector, CmdlineCollector},
    controller::{BootFsController, CmdlineController},
    scrutiny::prelude::*,
    std::sync::Arc,
};

plugin!(
    ZbiPlugin,
    PluginHooks::new(
        collectors! {
            "BootFsCollector" => BootFsCollector::default(),
            "CmdlineCollector" => CmdlineCollector::default(),
        },
        controllers! {
            "/zbi/bootfs" => BootFsController::default(),
            "/zbi/cmdline" => CmdlineController::default(),
        }
    ),
    vec![]
);
