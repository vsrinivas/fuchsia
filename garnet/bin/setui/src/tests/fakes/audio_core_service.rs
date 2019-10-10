// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
use crate::audio::{DEFAULT_VOLUME_LEVEL, DEFAULT_VOLUME_MUTED};
use crate::tests::fakes::base::Service;
use failure::{format_err, Error};
use fidl::endpoints::{ServerEnd, ServiceMarker};
use fidl_fuchsia_media::{AudioRenderUsage, Usage};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::TryStreamExt;
use parking_lot::RwLock;
use std::collections::HashMap;
use std::sync::Arc;

/// An implementation of audio core service that captures the set gains on
/// usages.
pub struct AudioCoreService {
    audio_streams: Arc<RwLock<HashMap<AudioRenderUsage, (f32, bool)>>>,
    ack_count: Arc<RwLock<HashMap<AudioRenderUsage, u32>>>,
}

impl AudioCoreService {
    pub fn new() -> Self {
        Self {
            audio_streams: Arc::new(RwLock::new(HashMap::new())),
            ack_count: Arc::new(RwLock::new(HashMap::new())),
        }
    }

    pub fn get_level_and_mute(&self, usage: AudioRenderUsage) -> Option<(f32, bool)> {
        get_level_and_mute(usage, &self.audio_streams)
    }

    pub fn get_ack_count(&self, usage: AudioRenderUsage) -> Option<u32> {
        if let Some(count) = (*self.ack_count.read()).get(&usage) {
            return Some(*count);
        }
        None
    }
}

impl Service for AudioCoreService {
    fn can_handle_service(&self, service_name: &str) -> bool {
        return service_name == fidl_fuchsia_media::AudioCoreMarker::NAME;
    }

    fn process_stream(&self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        if !self.can_handle_service(service_name) {
            return Err(format_err!("unsupported"));
        }

        let mut manager_stream =
            ServerEnd::<fidl_fuchsia_media::AudioCoreMarker>::new(channel).into_stream()?;

        let streams_clone = self.audio_streams.clone();
        let ack_count_clone = self.ack_count.clone();
        fasync::spawn(async move {
            while let Some(req) = manager_stream.try_next().await.unwrap() {
                #[allow(unreachable_patterns)]
                match req {
                    fidl_fuchsia_media::AudioCoreRequest::BindUsageVolumeControl {
                        usage,
                        volume_control,
                        control_handle: _,
                    } => {
                        if let Usage::RenderUsage(render_usage) = usage {
                            process_volume_control_stream(
                                volume_control,
                                render_usage,
                                streams_clone.clone(),
                                ack_count_clone.clone(),
                            );
                        }
                    }
                    _ => {}
                }
            }
        });

        Ok(())
    }
}

fn get_level_and_mute(
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
    ack_count: Arc<RwLock<HashMap<AudioRenderUsage, u32>>>,
) {
    let mut stream = volume_control.into_stream().expect("volume control stream error");
    fasync::spawn(async move {
        while let Some(req) = stream.try_next().await.unwrap() {
            #[allow(unreachable_patterns)]
            match req {
                fidl_fuchsia_media_audio::VolumeControlRequest::SetVolume {
                    volume,
                    control_handle,
                } => {
                    let mut curr_mute = DEFAULT_VOLUME_MUTED;
                    if let Some((_level, muted)) = get_level_and_mute(render_usage, &streams) {
                        (*streams.write()).insert(render_usage, (volume, muted));
                        curr_mute = muted;
                    } else {
                        (*streams.write()).insert(render_usage, (volume, DEFAULT_VOLUME_MUTED));
                    }
                    control_handle.send_on_volume_mute_changed(volume, curr_mute)
                        .expect("on volume mute changed");
                }
                fidl_fuchsia_media_audio::VolumeControlRequest::SetMute {
                    mute,
                    control_handle,
                } => {
                    let mut curr_level = DEFAULT_VOLUME_LEVEL;
                    if let Some((level, _muted)) = get_level_and_mute(render_usage, &streams) {
                        (*streams.write()).insert(render_usage, (level, mute));
                        curr_level = level;
                    } else {
                        (*streams.write()).insert(render_usage, (DEFAULT_VOLUME_LEVEL, mute));
                    }

                    control_handle.send_on_volume_mute_changed(curr_level, mute)
                        .expect("on volume mute changed");
                }
                fidl_fuchsia_media_audio::VolumeControlRequest::NotifyVolumeMuteChangedHandled {
                    control_handle: _
                } => {
                    let mut ack_count_lock = ack_count.write();
                    let mut new_count = 1;
                    if let Some(count) = (*ack_count_lock).get(&render_usage) {
                        new_count = *count + 1;
                    }
                    ack_count_lock.insert(render_usage, new_count);
                }
                _ => {}
            }
        }
    });
}
