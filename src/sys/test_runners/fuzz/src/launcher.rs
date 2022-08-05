// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    async_trait::async_trait,
    fidl::endpoints::Proxy,
    fidl_fuchsia_fuzzer as fuzzer, fidl_fuchsia_process as fproc,
    fuchsia_runtime::{HandleInfo, HandleType},
    fuchsia_zircon as zx,
    test_runners_elf_lib::launcher::ComponentLauncher,
    test_runners_lib::{
        elf::{Component, KernelError},
        errors::*,
        launch,
        logs::LoggerStream,
    },
    zx::HandleBased,
};

#[derive(Default)]
pub struct FuzzComponentLauncher {}

#[async_trait]
impl ComponentLauncher for FuzzComponentLauncher {
    /// Convenience wrapper around [`launch::launch_process`].
    async fn launch_process(
        &self,
        component: &Component,
        args: Vec<String>,
    ) -> Result<(zx::Process, launch::ScopedJob, LoggerStream, LoggerStream), RunTestError> {
        let mut args = args.clone();
        args.insert(0, component.url.clone());
        let registry = fuchsia_component::client::connect_to_protocol::<fuzzer::RegistrarMarker>()
            .map_err(launch::LaunchError::Launcher)?;
        let channel = registry.into_channel().expect("failed to take channel from proxy");
        let (loader_client, loader_server) =
            fidl::endpoints::create_endpoints().map_err(launch::LaunchError::Fidl)?;
        component.loader_service(loader_server);
        let executable_vmo = Some(component.executable_vmo()?);
        let result = launch::launch_process_with_separate_std_handles(launch::LaunchProcessArgs {
            bin_path: &component.binary,
            process_name: &component.name,
            job: Some(component.job.create_child_job().map_err(KernelError::CreateJob).unwrap()),
            ns: component.ns.clone(),
            args: Some(args),
            name_infos: None,
            environs: component.environ.clone(),
            handle_infos: Some(vec![fproc::HandleInfo {
                handle: channel.into_zx_channel().into_handle(),
                id: HandleInfo::new(HandleType::User0, 0).as_raw(),
            }]),
            loader_proxy_chan: Some(loader_client.into_channel()),
            executable_vmo,
        })
        .await?;
        Ok(result)
    }
}

impl FuzzComponentLauncher {
    pub fn new() -> Self {
        Self {}
    }
}
