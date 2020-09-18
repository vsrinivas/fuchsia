// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::core::{
        controllers::{blob::*, component::*, package::*, route::*, zbi::*},
        package_collector::*,
    },
    scrutiny::{
        collectors, controllers,
        engine::hook::PluginHooks,
        engine::plugin::{Plugin, PluginDescriptor},
        model::collector::DataCollector,
        model::controller::DataController,
        plugin,
    },
    std::sync::Arc,
};

plugin!(
    CorePlugin,
    PluginHooks::new(
        collectors! {
            "PackageDataCollector" => PackageDataCollector::new().unwrap(),
        },
        controllers! {
            "/component" => ComponentGraphController::default(),
            "/components" => ComponentsGraphController::default(),
            "/component/uses" => ComponentUsesGraphController::default(),
            "/component/used" => ComponentUsedGraphController::default(),
            "/component/manifest" => ComponentManifestGraphController::default(),
            "/packages" => PackagesGraphController::default(),
            "/routes" => RoutesGraphController::default(),
            "/blob" => BlobController::new(),
            "/zbi/sections" => ZbiSectionsController::default(),
            "/zbi/bootfs" => BootfsPathsController::default(),
            "/zbi/cmdline" => ZbiCmdlineController::default(),
        }
    ),
    vec![]
);
