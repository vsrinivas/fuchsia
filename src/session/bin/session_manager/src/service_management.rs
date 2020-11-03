// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::startup,
    anyhow::{Context as _, Error},
    fidl_fuchsia_component as fcomponent,
    fidl_fuchsia_input_injection::{
        InputDeviceRegistryMarker, InputDeviceRegistryProxy, InputDeviceRegistryRequest,
        InputDeviceRegistryRequestStream,
    },
    fidl_fuchsia_session::{LaunchSessionError, LauncherRequest, LauncherRequestStream},
    fuchsia_component::server::ServiceFs,
    fuchsia_zircon as zx,
    futures::{StreamExt, TryStreamExt},
};

/// The services exposed by the session manager.
enum ExposedServices {
    Launcher(LauncherRequestStream),
    InputDeviceRegistry(InputDeviceRegistryRequestStream),
}

/// Starts serving [`ExposedServices`] from `svc`.
///
/// This will return once the [`ServiceFs`] stops serving requests.
///
/// # Parameters
/// - `initial_url`: The initial URL of the launched session.
///
/// # Errors
/// Returns an error if there is an issue serving the `svc` directory handle.
pub async fn expose_services(
    initial_url: &mut String,
    mut input_injection_svc_channel: zx::Channel,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new_local();
    fs.dir("svc")
        .add_fidl_service(ExposedServices::Launcher)
        .add_fidl_service(ExposedServices::InputDeviceRegistry);
    fs.take_and_serve_directory_handle()?;

    // Note: because this loop processes a single request at a time, it's possible for a connection
    // to either protocol to starve out requests to the other protocol (as well as other
    // clients of the same protocol).
    //
    // That means that, for now, forward progress depends on each caller closing its connection
    // as soon as that caller has completed a single request.
    while let Some(service_request) = fs.next().await {
        match service_request {
            ExposedServices::Launcher(request_stream) => {
                handle_session_manager_request_stream(
                    request_stream,
                    initial_url,
                    &mut input_injection_svc_channel,
                )
                .await
                .expect("Session launcher request stream got an error.");
            }
            ExposedServices::InputDeviceRegistry(request_stream) => {
                let (downstream_proxy, server_end) =
                    fidl::endpoints::create_proxy::<InputDeviceRegistryMarker>()
                        .expect("Failed to create InputDeviceRegistryProxy");
                fdio::service_connect_at(
                    &input_injection_svc_channel,
                    "fuchsia.input.injection.InputDeviceRegistry",
                    server_end.into_channel(),
                )
                .expect("Failed to connect to downstream service");

                handle_input_device_registry_request_stream(request_stream, downstream_proxy)
                    .await
                    .expect("Input device registry request stream got an error.");
            }
        }
    }

    Ok(())
}

async fn handle_input_device_registry_request_stream(
    mut request_stream: InputDeviceRegistryRequestStream,
    downstream_proxy: InputDeviceRegistryProxy,
) -> Result<(), Error> {
    while let Some(request) = request_stream
        .try_next()
        .await
        .context("Error handling input device registry request stream")?
    {
        match request {
            InputDeviceRegistryRequest::Register { device, .. } => {
                downstream_proxy
                    .register(device)
                    .context("Error handling InputDeviceRegistryRequest::Register")?;
            }
        }
    }
    Ok(())
}

/// Handles calls to launch_session() and returns a Result containing
/// the most recently launched session url or a LaunchSessionError.
///
/// # Parameters
/// - session_url: An optional session url.
async fn handle_launch_session_request(
    session_url: Option<String>,
) -> Result<(String, zx::Channel), LaunchSessionError> {
    if let Some(session_url) = session_url {
        match startup::launch_session(&session_url).await {
            Ok(input_injection_svc_channel) => {
                return Ok((session_url, input_injection_svc_channel))
            }
            Err(err) => match err {
                startup::StartupError::NotCreated {
                    name: _,
                    collection: _,
                    url: _,
                    err: sys_err,
                } => match sys_err {
                    fcomponent::Error::InstanceCannotResolve => {
                        return Err(LaunchSessionError::NotFound)
                    }
                    _ => return Err(LaunchSessionError::Failed),
                },
                _ => return Err(LaunchSessionError::Failed),
            },
        };
    } else {
        return Err(LaunchSessionError::NotFound);
    }
}

/// Serves a specified [`LauncherRequestStream`].
///
/// # Parameters
/// - `request_stream`: the LauncherRequestStream.
/// - `session_url`: the URL of the most recently launched session.
///
/// # Errors
/// When an error is encountered reading from the request stream.
async fn handle_session_manager_request_stream(
    mut request_stream: LauncherRequestStream,
    session_url: &mut String,
    input_injection_svc_channel: &mut zx::Channel,
) -> Result<(), Error> {
    while let Some(request) =
        request_stream.try_next().await.context("Error handling launcher request stream")?
    {
        match request {
            LauncherRequest::LaunchSession { configuration, responder } => {
                let result = handle_launch_session_request(configuration.session_url).await;
                match result {
                    Ok((new_session_url, new_input_injection_svc_channel)) => {
                        let _ = responder.send(&mut Ok(()));
                        *session_url = new_session_url;
                        *input_injection_svc_channel = new_input_injection_svc_channel;
                    }
                    Err(err) => {
                        let _ = responder.send(&mut Err(err));
                    }
                }
            }
            LauncherRequest::RestartSession { responder } => {
                let result = handle_launch_session_request(Some(session_url.clone())).await;

                match result {
                    Ok((new_session_url, new_input_injection_svc_channel)) => {
                        let _ = responder.send(&mut Ok(()));
                        *session_url = new_session_url;
                        *input_injection_svc_channel = new_input_injection_svc_channel;
                    }
                    Err(err) => {
                        let _ = responder.send(&mut Err(err));
                    }
                }
            }
        };
    }
    Ok(())
}

// Ideally, this module's tests would exercise its public interface. However, the public
// interface of this module is intimately tied to FIDL, which makes the public interface hard
// to exercise with unit tests. As a compromise, the unit tests below exercise this module's
// non-FIDL-dependent internal functions instead.
#[cfg(test)]
mod tests {
    use {
        super::{
            handle_input_device_registry_request_stream, InputDeviceRegistryMarker,
            InputDeviceRegistryRequest,
        },
        fidl::endpoints::{create_endpoints, create_proxy_and_stream},
        fidl_fuchsia_input_report::InputDeviceMarker,
        fuchsia_async as fasync,
        futures::prelude::*,
        matches::assert_matches,
    };

    #[fasync::run_until_stalled(test)]
    async fn handle_input_device_registry_request_stream_propagates_request_to_downstream_service()
    {
        let (local_proxy, local_request_stream) =
            create_proxy_and_stream::<InputDeviceRegistryMarker>()
                .expect("Failed to create local InputDeviceRegistry proxy and stream");
        let (downstream_proxy, mut downstream_request_stream) =
            create_proxy_and_stream::<InputDeviceRegistryMarker>()
                .expect("Failed to create downstream InputDeviceRegistry proxy and stream");
        let mut num_devices_registered = 0;

        let local_server_fut =
            handle_input_device_registry_request_stream(local_request_stream, downstream_proxy);
        let downstream_server_fut = async {
            while let Some(request) = downstream_request_stream.try_next().await.unwrap() {
                match request {
                    InputDeviceRegistryRequest::Register { .. } => num_devices_registered += 1,
                }
            }
        };

        let (input_device_client, _input_device_server) = create_endpoints::<InputDeviceMarker>()
            .expect("Failed to create InputDevice endpoints");
        local_proxy
            .register(input_device_client)
            .expect("Failed to send registration request locally");
        std::mem::drop(local_proxy); // Drop proxy to terminate `server_fut`.

        assert_matches!(local_server_fut.await, Ok(()));
        downstream_server_fut.await;
        assert_eq!(num_devices_registered, 1);
    }
}
