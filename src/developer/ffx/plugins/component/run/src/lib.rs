// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result,
    ffx_component::{create_component_instance, verify_fuchsia_pkg_cm_url},
    ffx_component_run_args::RunComponentCommand,
    ffx_core::ffx_plugin,
    fidl_fuchsia_developer_remotecontrol as rc,
    fidl_fuchsia_sys2::RealmProxy,
};

const COLLECTION_NAME: &'static str = "ffx-laboratory";

#[ffx_plugin(RealmProxy = "core:in:fuchsia.sys2.Realm")]
pub async fn run_component(
    _rcs_proxy: rc::RemoteControlProxy,
    realm_proxy: RealmProxy,
    run: RunComponentCommand,
) -> Result<()> {
    run_component_cmd(realm_proxy, run).await
}

async fn run_component_cmd(realm_proxy: RealmProxy, run: RunComponentCommand) -> Result<()> {
    let manifest_name = verify_fuchsia_pkg_cm_url(run.url.as_str())?;

    let name = if let Some(name) = run.name {
        // Use a custom name provided in the command line
        name
    } else {
        // Attempt to use the manifest name as the instance name
        manifest_name
    };

    println!("Creating component instance: {}", name);
    create_component_instance(&realm_proxy, name, run.url, COLLECTION_NAME.to_string()).await
}
