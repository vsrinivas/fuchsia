// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_media::AudioCoreRequestStream;
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_component_test::LocalComponentHandles;
use futures::StreamExt;
use futures::TryStreamExt;

pub(crate) async fn audio_core_service_mock(handles: LocalComponentHandles) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let _: &mut ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(move |mut stream: AudioCoreRequestStream| {
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
                            fasync::Task::spawn(async move {
                                while let Some(_) = stream.try_next().await.unwrap() {
                                    // Consume requests but ignore them.
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
    let _: &mut ServiceFs<_> = fs.serve_connection(handles.outgoing_dir.into_channel()).unwrap();
    fs.collect::<()>().await;
    Ok(())
}
