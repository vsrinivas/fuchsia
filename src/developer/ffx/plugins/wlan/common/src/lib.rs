// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl::endpoints::{create_endpoints, create_proxy},
    fidl_fuchsia_wlan_policy as wlan_policy,
};

pub mod args;

/// Communicates with the client policy provider to get the components required to get a client
/// controller.
pub async fn get_client_controller(
    policy_provider: wlan_policy::ClientProviderProxy,
) -> Result<(wlan_policy::ClientControllerProxy, wlan_policy::ClientStateUpdatesRequestStream), Error>
{
    let (client_controller, server_end) =
        create_proxy::<wlan_policy::ClientControllerMarker>().unwrap();
    let (update_client_end, update_server_end) =
        create_endpoints::<wlan_policy::ClientStateUpdatesMarker>().unwrap();
    let () = policy_provider.get_controller(server_end, update_client_end)?;
    let update_stream = update_server_end.into_stream()?;

    Ok((client_controller, update_stream))
}

/// Communicates with the client listener service to get a stream of client state updates.
pub fn get_client_listener_stream(
    listener: wlan_policy::ClientListenerProxy,
) -> Result<wlan_policy::ClientStateUpdatesRequestStream, Error> {
    let (client_end, server_end) =
        create_endpoints::<wlan_policy::ClientStateUpdatesMarker>().unwrap();
    listener.get_listener(client_end)?;
    let server_stream = server_end.into_stream()?;
    Ok(server_stream)
}

/// Communicates with the AccessPointProvider service to create an access point controller and an
/// access point listener stream.
pub async fn get_ap_controller(
    policy_provider: wlan_policy::AccessPointProviderProxy,
) -> Result<
    (wlan_policy::AccessPointControllerProxy, wlan_policy::AccessPointStateUpdatesRequestStream),
    Error,
> {
    let (ap_controller, server_end) =
        create_proxy::<wlan_policy::AccessPointControllerMarker>().unwrap();
    let (update_client_end, update_server_end) =
        create_endpoints::<wlan_policy::AccessPointStateUpdatesMarker>().unwrap();
    let () = policy_provider.get_controller(server_end, update_client_end)?;
    let update_stream = update_server_end.into_stream()?;

    Ok((ap_controller, update_stream))
}

/// Gets a listener to observe AP state update events.
pub fn get_ap_listener_stream(
    listener: wlan_policy::AccessPointListenerProxy,
) -> Result<wlan_policy::AccessPointStateUpdatesRequestStream, Error> {
    let (client_end, server_end) =
        create_endpoints::<wlan_policy::AccessPointStateUpdatesMarker>().unwrap();
    listener.get_listener(client_end)?;
    let server_stream = server_end.into_stream()?;
    Ok(server_stream)
}
