// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub mod collection;
mod controller;
mod package;
mod util;

use {
    crate::core::{
        controller::{blob::*, component::*, package::*, route::*, sysmgr::*, zbi::*},
        package::collector::*,
    },
    scrutiny::prelude::*,
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
            "/sysmgr/services" => SysmgrServicesController::default(),
            "/zbi/sections" => ZbiSectionsController::default(),
            "/zbi/bootfs" => BootfsPathsController::default(),
            "/zbi/cmdline" => ZbiCmdlineController::default(),
        }
    ),
    vec![]
);
