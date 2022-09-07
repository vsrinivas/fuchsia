// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_ui_policy::{
    DeviceListenerRegistryRequest, DeviceListenerRegistryRequestStream, MediaButtonsListenerProxy,
};
use fuchsia_async as fasync;
use fuchsia_component::server::ServiceFs;
use fuchsia_component_test::LocalComponentHandles;
use futures::channel::mpsc::{Receiver, Sender};
use futures::lock::Mutex;
use futures::StreamExt;
use futures::TryStreamExt;
use std::sync::Arc;

pub type ButtonEventSender = Sender<MediaButtonsListenerProxy>;
pub type ButtonEventReceiver = Receiver<MediaButtonsListenerProxy>;

pub async fn input_device_registry_mock(
    handles: LocalComponentHandles,
    listener_sender: Arc<Mutex<ButtonEventSender>>,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let _ =
        fs.dir("svc").add_fidl_service(move |mut stream: DeviceListenerRegistryRequestStream| {
            fasync::Task::spawn({
                let listener_sender = Arc::clone(&listener_sender);
                async move {
                    while let Ok(Some(request)) = stream.try_next().await {
                        match request {
                            DeviceListenerRegistryRequest::RegisterListener {
                                listener,
                                responder,
                            } => {
                                if let Ok(proxy) = listener.into_proxy() {
                                    listener_sender
                                        .lock()
                                        .await
                                        .try_send(proxy)
                                        .expect("test should listen");
                                    // Acknowledge the registration.
                                    responder.send().expect("failed to ack RegisterListener call");
                                }
                            }
                            _ => {
                                panic!("Unsupported request {request:?}")
                            }
                        }
                    }
                }
            })
            .detach()
        });

    let _ = fs.serve_connection(handles.outgoing_dir)?;
    fs.collect::<()>().await;
    Ok(())
}
