// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

mod collection;
mod collector;
mod controller;

pub use collection::StaticPkgsCollection;

use {
    collector::StaticPkgsCollector, controller::ExtractStaticPkgsController, scrutiny::prelude::*,
    std::sync::Arc,
};

plugin!(
    StaticPkgsPlugin,
    PluginHooks::new(
        collectors! {
            "StaticPkgsCollector" => StaticPkgsCollector::default(),
        },
        controllers! {
            "/static/pkgs" => ExtractStaticPkgsController::default(),
        }
    ),
    vec![PluginDescriptor::new("DevmgrConfigPlugin")]
);
