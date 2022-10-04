// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_media::AudioCoreRequestStream;
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_component_test::LocalComponentHandles;
use futures::channel::mpsc::Sender;
use futures::{SinkExt, StreamExt, TryStreamExt};

pub(crate) async fn audio_core_service_mock(
    handles: LocalComponentHandles,
    usage_control_bound_sender: Sender<()>,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let _: &mut ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(move |mut stream: AudioCoreRequestStream| {
            let usage_control_bound_sender = usage_control_bound_sender.clone();
            fasync::Task::spawn(async move {
                while let Ok(Some(req)) = stream.try_next().await {
                    // Support future expansion of FIDL.
                    #[allow(unreachable_patterns)]
                    match req {
                        fidl_fuchsia_media::AudioCoreRequest::BindUsageVolumeControl {
                            usage: _,
                            volume_control,
                            control_handle: _,
                        } => {
                            let mut stream =
                                volume_control.into_stream().expect("volume control stream error");
                            let mut usage_control_bound_sender = usage_control_bound_sender.clone();
                            fasync::Task::spawn(async move {
                                while let Some(_) = stream.try_next().await.unwrap() {
                                    // Consume requests but ignore them. Signal that requests are
                                    // received.
                                    usage_control_bound_sender
                                        .send(())
                                        .await
                                        .expect("Request is received");
                                }
                            })
                            .detach();
                        }
                        _ => {}
                    }
                }
            })
            .detach();
        });
    let _: &mut ServiceFs<_> = fs.serve_connection(handles.outgoing_dir).unwrap();
    fs.collect::<()>().await;
    Ok(())
}
