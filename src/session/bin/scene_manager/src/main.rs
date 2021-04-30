// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::Error,
    fidl_fuchsia_input_injection::InputDeviceRegistryRequestStream,
    fidl_fuchsia_session_scene::{ManagerRequest, ManagerRequestStream},
    fidl_fuchsia_ui_policy::PointerCaptureListenerHackProxy,
    fidl_fuchsia_ui_scenic::ScenicMarker,
    fuchsia_async as fasync,
    fuchsia_component::{client::connect_to_protocol, server::ServiceFs},
    fuchsia_syslog::fx_log_warn,
    futures::lock::Mutex,
    futures::{StreamExt, TryStreamExt},
    scene_management::{self, SceneManager},
    std::sync::Arc,
};

mod input_device_registry_server;
mod input_pipeline;
#[macro_use]
mod input_testing_utilities;
mod mouse_pointer_hack;
mod touch_pointer_hack;

enum ExposedServices {
    Manager(ManagerRequestStream),
    InputDeviceRegistry(InputDeviceRegistryRequestStream),
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    fuchsia_syslog::init_with_tags(&["scene_manager"]).expect("Failed to init syslog");

    let mut fs = ServiceFs::new_local();
    fs.dir("svc").add_fidl_service(ExposedServices::Manager);
    fs.dir("svc").add_fidl_service(ExposedServices::InputDeviceRegistry);
    fs.take_and_serve_directory_handle()?;

    let (input_device_registry_server, input_device_registry_request_stream_receiver) =
        input_device_registry_server::make_server_and_receiver();

    let mut input_receiver = Some(input_device_registry_request_stream_receiver);

    while let Some(service_request) = fs.next().await {
        match service_request {
            ExposedServices::Manager(request_stream) => {
                if let Some(input_receiver) = input_receiver {
                    let scenic = connect_to_protocol::<ScenicMarker>()?;
                    let scene_manager =
                        scene_management::FlatSceneManager::new(scenic, None, None).await?;
                    fasync::Task::local(handle_manager_request_stream(
                        request_stream,
                        scene_manager,
                        input_receiver,
                    ))
                    .detach();
                }
                input_receiver = None;
            }
            ExposedServices::InputDeviceRegistry(request_stream) => {
                match &input_device_registry_server.handle_request(request_stream).await {
                    Ok(()) => (),
                    Err(e) => {
                        // If `handle_request()` returns `Err`, then the `unbounded_send()` call
                        // from `handle_request()` failed with either:
                        // * `TrySendError::SendErrorKind::Full`, or
                        // * `TrySendError::SendErrorKind::Disconnected`.
                        //
                        // These are unexpected, because:
                        // * `Full` can't happen, because `InputDeviceRegistryServer`
                        //   uses an `UnboundedSender`.
                        // * `Disconnected` is highly unlikely, because the corresponding
                        //   `UnboundedReceiver` lives in `main::input_fut`, and `input_fut`'s
                        //   lifetime is nearly as long as `input_device_registry_server`'s.
                        //
                        // Nonetheless, InputDeviceRegistry isn't critical to production use.
                        // So we just log the error and move on.
                        fx_log_warn!(
                            "failed to forward InputDeviceRegistryRequestStream: {:?}; \
                                must restart to enable input injection",
                            e
                        )
                    }
                }
            }
        }
    }

    Ok(())
}

pub async fn handle_manager_request_stream(
    mut request_stream: ManagerRequestStream,
    scene_manager: scene_management::FlatSceneManager,
    input_device_registry_request_stream_receiver: futures::channel::mpsc::UnboundedReceiver<
        InputDeviceRegistryRequestStream,
    >,
) {
    let scene_manager = Arc::new(Mutex::new(scene_manager));
    let listeners: Vec<PointerCaptureListenerHackProxy> = vec![];
    let pointer_listeners = Arc::new(Mutex::new(listeners));

    if let Ok(input_pipeline) = input_pipeline::handle_input(
        scene_manager.clone(),
        pointer_listeners.clone(),
        input_device_registry_request_stream_receiver,
    )
    .await
    {
        fasync::Task::local(input_pipeline.handle_input_events()).detach();
    }

    while let Ok(Some(request)) = request_stream.try_next().await {
        match request {
            ManagerRequest::SetRootView { view_provider, responder, .. } => {
                if let Ok(proxy) = view_provider.into_proxy() {
                    let mut scene_manager = scene_manager.lock().await;
                    let mut r = scene_manager.set_root_view(proxy).await.unwrap();
                    let _ = responder.send(&mut r);
                }
            }
            ManagerRequest::RequestFocus { mut view_ref, responder, .. } => {
                let scene_manager = scene_manager.lock().await;
                if let Ok(mut response) = scene_manager.focuser.request_focus(&mut view_ref).await {
                    let _ = responder.send(&mut response);
                }
            }
            ManagerRequest::CapturePointerEvents { listener, .. } => {
                pointer_listeners.lock().await.push(listener.into_proxy().unwrap());
            }
        };
    }
}
