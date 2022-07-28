// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl::endpoints::ServerEnd;
use fidl_fuchsia_media::{AudioCoreRequestStream, AudioRenderUsage, Usage};
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_component_test::LocalComponentHandles;
use futures::StreamExt;
use futures::TryStreamExt;
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;

pub(crate) async fn audio_core_service_mock(
    handles: LocalComponentHandles,
    // TODO(fxbug.dev/105325): replace with Arc<Mutex<...>>
    audio_streams: Arc<RwLock<HashMap<AudioRenderUsage, (f32, bool)>>>,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let _: &mut ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(move |mut stream: AudioCoreRequestStream| {
            let streams_clone = audio_streams.clone();
            fasync::Task::spawn(async move {
                while let Ok(Some(req)) = stream.try_next().await {
                    // Support future expansion of FIDL.
                    #[allow(unreachable_patterns)]
                    match req {
                        fidl_fuchsia_media::AudioCoreRequest::BindUsageVolumeControl {
                            usage: Usage::RenderUsage(render_usage),
                            volume_control,
                            control_handle: _,
                        } => {
                            process_volume_control_stream(
                                volume_control,
                                render_usage,
                                streams_clone.clone(),
                            );
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

pub(crate) fn get_level_and_mute(
    usage: AudioRenderUsage,
    streams: &RwLock<HashMap<AudioRenderUsage, (f32, bool)>>,
) -> Option<(f32, bool)> {
    if let Some((level, muted)) = (*streams.read()).get(&usage) {
        return Some((*level, *muted));
    }
    None
}

fn process_volume_control_stream(
    volume_control: ServerEnd<fidl_fuchsia_media_audio::VolumeControlMarker>,
    render_usage: AudioRenderUsage,
    streams: Arc<RwLock<HashMap<AudioRenderUsage, (f32, bool)>>>,
) {
    let mut stream = volume_control.into_stream().expect("volume control stream error");
    fasync::Task::spawn(async move {
        while let Some(req) = stream.try_next().await.unwrap() {
            #[allow(unreachable_patterns)]
            match req {
                fidl_fuchsia_media_audio::VolumeControlRequest::SetVolume {
                    volume,
                    control_handle,
                } => {
                    let (_level, muted) =
                        get_level_and_mute(render_usage, &streams).expect("stream in map");
                    let _ = (*streams.write()).insert(render_usage, (volume, muted));

                    control_handle
                        .send_on_volume_mute_changed(volume, muted)
                        .expect("on volume mute changed");
                }
                fidl_fuchsia_media_audio::VolumeControlRequest::SetMute {
                    mute,
                    control_handle,
                } => {
                    let (level, _muted) =
                        get_level_and_mute(render_usage, &streams).expect("stream in map");
                    let _ = (*streams.write()).insert(render_usage, (level, mute));

                    control_handle
                        .send_on_volume_mute_changed(level, mute)
                        .expect("on volume mute changed");
                }
                _ => {}
            }
        }
    })
    .detach();
}
