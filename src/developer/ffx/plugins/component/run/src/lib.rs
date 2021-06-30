// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{anyhow, Result},
    ffx_component_run_args::RunComponentCommand,
    ffx_core::ffx_plugin,
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_io as fio,
    fidl_fuchsia_sys2::{
        ChildDecl, ChildRef, CollectionRef, CreateChildArgs, RealmProxy, StartupMode,
    },
    fuchsia_url::pkg_url::PkgUrl,
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
    let url = match PkgUrl::parse(run.url.as_str()) {
        Ok(url) => url,
        Err(e) => {
            return Err(anyhow!("URL parsing error: {:?}", e));
        }
    };

    let resource = if let Some(resource) = url.resource() {
        resource
    } else {
        return Err(anyhow!("URL does not contain a path to a manifest"));
    };

    let manifest = if let Some(manifest) = resource.split('/').last() {
        manifest
    } else {
        return Err(anyhow!("Could not extract manifest filename from URL"));
    };

    let manifest_name = if let Some(name) = manifest.strip_suffix(".cm") {
        name
    } else if manifest.ends_with(".cmx") {
        return Err(anyhow!(
            "{} is a legacy component manifest. Run it using `ffx component run-legacy`",
            manifest
        ));
    } else {
        return Err(anyhow!(
            "{} is not a component manifest! Component manifests must end in the `cm` extension.",
            manifest
        ));
    };

    let name = if let Some(name) = run.name {
        // Use a custom name provided in the command line
        name
    } else {
        // Attempt to use the manifest name as the instance name
        manifest_name.to_string()
    };

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

    let child_args = CreateChildArgs { numbered_handles: None, ..CreateChildArgs::EMPTY };
    realm_proxy
        .create_child(&mut collection, decl, child_args)
        .await?
        .map_err(|e| anyhow!("Error creating child: {:?}", e))?;

    let (_, server_end) = create_endpoints::<fio::DirectoryMarker>()?;
    realm_proxy
        .bind_child(&mut child_ref, server_end)
        .await?
        .map_err(|e| anyhow!("Error binding to child: {:?}", e))?;
    return Ok(());
}
