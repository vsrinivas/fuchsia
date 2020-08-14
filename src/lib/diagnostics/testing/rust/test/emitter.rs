// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl_fuchsia_sys::LauncherMarker;
use fuchsia_component::{
    client::{connect_to_service, launch_with_options, LaunchOptions},
    server::ServiceFs,
};
use fuchsia_inspect::*;
use futures::prelude::*;
use log::info;

#[fuchsia_async::run_singlethreaded]
async fn main() {
    fuchsia_syslog::init().unwrap();
    info!("emitter started");
    let root = component::inspector().root();
    root.record_int("other_int", 7);

    let mut fs = ServiceFs::new();
    component::inspector().serve(&mut fs).unwrap();

    fs.take_and_serve_directory_handle().unwrap();

    info!("launching child");
    let launcher = connect_to_service::<LauncherMarker>().unwrap();
    let _child = launch_with_options(
        &launcher,
        "fuchsia-pkg://fuchsia.com/diagnostics-testing-tests#meta/inspect_test_component.cmx"
            .to_owned(),
        None,
        LaunchOptions::new(),
    )
    .unwrap();

    fs.collect::<()>().await;
}
