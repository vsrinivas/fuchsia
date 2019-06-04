// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use fidl::endpoints::ServiceMarker;
use fidl_fuchsia_cobalt::LoggerFactoryMarker;
use fidl_fuchsia_fonts::ProviderMarker;
use fidl_fuchsia_ledger_cloud::CloudProviderMarker;
use fidl_fuchsia_logger::LogSinkMarker;
use fidl_fuchsia_sys::EnvironmentControllerProxy;
use fidl_fuchsia_tracelink::RegistryMarker;
use fidl_fuchsia_ui_input::ImeServiceMarker;
use fidl_fuchsia_ui_scenic::ScenicMarker;
use fidl_fuchsia_vulkan_loader::LoaderMarker;
use fuchsia_component::{
    client::{App as LaunchedApp, LaunchOptions},
    fuchsia_single_component_package_url,
    server::{ServiceFs, ServiceObj},
};
use fuchsia_syslog::fx_log_err;
use std::sync::Arc;

const SESSIONMGR_URI: &str = fuchsia_single_component_package_url!("sessionmgr");
const FLAG_USE_CLOUD_PROVIDER_FROM_ENV: &str = "--use_cloud_provider_from_environment";

pub fn make_replica_env(
    replica_id: &str,
    cloud_provider_app: Arc<LaunchedApp>,
) -> Result<
    (ServiceFs<ServiceObj<'static, ()>>, EnvironmentControllerProxy, LaunchedApp),
    failure::Error,
> {
    // Configure disk directory.
    let data_origin = format!("/data/voila/{}", replica_id);
    std::fs::create_dir_all(data_origin.clone())?;
    let mut launch_options = LaunchOptions::new();
    launch_options.add_dir_to_namespace("/data".to_string(), std::fs::File::open(data_origin)?)?;

    let mut fs = ServiceFs::new();
    fs.add_service_at(CloudProviderMarker::NAME, move |chan| {
        cloud_provider_app
            .pass_to_service::<CloudProviderMarker>(chan)
            .unwrap_or_else(|e| fx_log_err!("failed to pass cloud provider request {:?}", e));
        None
    })
    .add_proxy_service::<LogSinkMarker, _>()
    .add_proxy_service::<LoggerFactoryMarker, _>()
    .add_proxy_service::<ScenicMarker, _>()
    .add_proxy_service::<ImeServiceMarker, _>()
    .add_proxy_service::<LoaderMarker, _>()
    .add_proxy_service::<ProviderMarker, _>()
    .add_proxy_service::<RegistryMarker, _>();
    let (environment_ctrl, app) = fs.launch_component_in_nested_environment_with_options(
        SESSIONMGR_URI.to_string(),
        Some(vec![FLAG_USE_CLOUD_PROVIDER_FROM_ENV.to_string()]),
        launch_options,
        replica_id,
    )?;

    Ok((fs, environment_ctrl, app))
}
