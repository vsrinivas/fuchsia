// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use {
    crate::service_context::ServiceContextHandle,
    anyhow::Error,
    fidl::endpoints::create_request_stream,
    fidl_fuchsia_ui_input::MediaButtonsEvent,
    fidl_fuchsia_ui_policy::{
        DeviceListenerRegistryMarker, MediaButtonsListenerMarker, MediaButtonsListenerRequest,
    },
    fuchsia_async as fasync,
    futures::StreamExt,
};

/// Method for listening to media button changes. Changes will be reported back
/// on the supplied sender.
pub async fn monitor_mic_mute(
    service_context_handle: ServiceContextHandle,
    sender: futures::channel::mpsc::UnboundedSender<MediaButtonsEvent>,
) -> Result<(), Error> {
    let service_result =
        service_context_handle.lock().await.connect::<DeviceListenerRegistryMarker>();

    let presenter_service = match service_result {
        Ok(service) => service,
        Err(err) => {
            return Err(err);
        }
    };

    let (client_end, mut stream) = create_request_stream::<MediaButtonsListenerMarker>().unwrap();

    presenter_service
        .register_media_buttons_listener(client_end)
        .expect("Could not register as listener for media buttons");

    fasync::spawn(async move {
        while let Some(Ok(media_request)) = stream.next().await {
            // Support future expansion of FIDL
            #[allow(unreachable_patterns)]
            match media_request {
                MediaButtonsListenerRequest::OnMediaButtonsEvent { event, control_handle: _ } => {
                    sender.unbounded_send(event).ok();
                }
                _ => {}
            }
        }
    });

    return Ok(());
}
