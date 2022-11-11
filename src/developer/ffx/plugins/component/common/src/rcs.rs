// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Result, errors::ffx_error, fidl::endpoints::create_proxy,
    fidl_fuchsia_developer_remotecontrol as rc, fidl_fuchsia_sys2 as fsys,
    fuchsia_zircon_status::Status,
};

/// Obtain the root LifecycleController protocol using the RemoteControl protocol.
pub async fn connect_to_lifecycle_controller(
    rcs_proxy: &rc::RemoteControlProxy,
) -> Result<fsys::LifecycleControllerProxy> {
    let (lifecycle_controller, server_end) = create_proxy::<fsys::LifecycleControllerMarker>()?;
    rcs_proxy
        .root_lifecycle_controller(server_end)
        .await?
        .map_err(|i| ffx_error!("Could not open LifecycleController: {}", Status::from_raw(i)))?;
    Ok(lifecycle_controller)
}

/// Obtain the root RealmQuery protocol using the RemoteControl protocol.
pub async fn connect_to_realm_query(
    rcs_proxy: &rc::RemoteControlProxy,
) -> Result<fsys::RealmQueryProxy> {
    let (realm_query, server_end) = create_proxy::<fsys::RealmQueryMarker>()?;
    rcs_proxy
        .root_realm_query(server_end)
        .await?
        .map_err(|i| ffx_error!("Could not open RealmQuery: {}", Status::from_raw(i)))?;
    Ok(realm_query)
}

/// Obtain the root RealmExplorer protocol using the RemoteControl protocol.
pub async fn connect_to_realm_explorer(
    rcs_proxy: &rc::RemoteControlProxy,
) -> Result<fsys::RealmExplorerProxy> {
    let (realm_explorer, server_end) = create_proxy::<fsys::RealmExplorerMarker>()?;
    rcs_proxy
        .root_realm_explorer(server_end)
        .await?
        .map_err(|i| ffx_error!("Could not open RealmExplorer: {}", Status::from_raw(i)))?;
    Ok(realm_explorer)
}

/// Obtain the root RouteValidator protocol using the RemoteControl protocol.
pub async fn connect_to_route_validator(
    rcs_proxy: &rc::RemoteControlProxy,
) -> Result<fsys::RouteValidatorProxy> {
    let (route_validator, server_end) = create_proxy::<fsys::RouteValidatorMarker>()?;
    rcs_proxy
        .root_route_validator(server_end)
        .await?
        .map_err(|i| ffx_error!("Could not open RouteValidator: {}", Status::from_raw(i)))?;
    Ok(route_validator)
}
