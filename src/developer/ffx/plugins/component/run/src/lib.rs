// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    ffx_component_run_args::RunComponentCommand,
    ffx_core::ffx_plugin,
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2::{ChildDecl, ChildRef, CollectionRef, RealmProxy, StartupMode},
    rand::Rng,
};

const COLLECTION_NAME: &'static str = "ffx-laboratory";

#[ffx_plugin(RealmProxy = "core:expose:fuchsia.sys2.Realm")]
pub async fn run_component(
    _rcs_proxy: rc::RemoteControlProxy,
    realm_proxy: RealmProxy,
    run: RunComponentCommand,
) -> Result<()> {
    run_component_cmd(realm_proxy, run).await
}

async fn run_component_cmd(realm_proxy: RealmProxy, run: RunComponentCommand) -> Result<()> {
    if !run.url.ends_with(".cm") {
        return Err(anyhow!(
            "Invalid component URL! For legacy components, use `fx component run-legacy`."
        ));
    }

    let mut rng = rand::thread_rng();
    let id: u16 = rng.gen();
    let name = format!("C{}", id);
    println!("Creating component instance: {}", name);

    let mut collection = CollectionRef { name: COLLECTION_NAME.to_string() };
    let decl = ChildDecl {
        name: Some(name.clone()),
        url: Some(run.url.clone()),
        startup: Some(StartupMode::Lazy),
        environment: None,
        ..ChildDecl::EMPTY
    };

    let mut child_ref = ChildRef { name, collection: Some(COLLECTION_NAME.to_string()) };

    realm_proxy
        .create_child(&mut collection, decl)
        .await?
        .map_err(|e| anyhow!("Error creating child: {:?}", e))?;

    let (_, server_end) = create_endpoints::<fio::DirectoryMarker>()?;
    realm_proxy
        .bind_child(&mut child_ref, server_end)
        .await?
        .map_err(|e| anyhow!("Error binding to child: {:?}", e))?;
    return Ok(());
}
