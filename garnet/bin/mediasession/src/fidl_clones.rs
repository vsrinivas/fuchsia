// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//! Remove this file entirely when FIDL-526 is fixed.

use fidl_fuchsia_math::*;
use fidl_fuchsia_media::*;
use fidl_fuchsia_mediasession::*;

pub struct Clonable<T>(pub T);

impl Clone for Clonable<PlaybackStatus> {
    fn clone(&self) -> Self {
        Clonable(clone_playback_status(&self.0))
    }
}

pub fn clone_playback_status(playback_status: &PlaybackStatus) -> PlaybackStatus {
    PlaybackStatus {
        duration: playback_status.duration.clone(),
        playback_state: playback_status.playback_state.clone(),
        playback_function: playback_status.playback_function.as_ref().map(|function| {
            TimelineFunction {
                subject_time: function.subject_time,
                reference_time: function.reference_time,
                subject_delta: function.subject_delta,
                reference_delta: function.reference_delta,
            }
        }),
        repeat_mode: playback_status.repeat_mode.clone(),
        shuffle_on: playback_status.shuffle_on,
        has_next_item: playback_status.has_next_item,
        has_prev_item: playback_status.has_prev_item,
        error: playback_status
            .error
            .as_ref()
            .map(|error| Error { code: error.code, description: error.description.clone() }),
    }
}

impl Clone for Clonable<Metadata> {
    fn clone(&self) -> Self {
        Clonable(clone_metadata(&self.0))
    }
}

pub fn clone_metadata(metadata: &Metadata) -> Metadata {
    Metadata {
        properties: metadata
            .properties
            .iter()
            .map(|p| Property { label: p.label.clone(), value: p.value.clone() })
            .collect(),
    }
}

impl Clone for Clonable<PlaybackCapabilities> {
    fn clone(&self) -> Self {
        Clonable(clone_playback_capabilities(&self.0))
    }
}

pub fn clone_playback_capabilities(
    playback_capabilities: &PlaybackCapabilities,
) -> PlaybackCapabilities {
    PlaybackCapabilities {
        flags: playback_capabilities.flags,
        supported_skip_intervals: playback_capabilities.supported_skip_intervals.clone(),
        supported_playback_rates: playback_capabilities.supported_playback_rates.clone(),
        supported_repeat_modes: playback_capabilities.supported_repeat_modes.clone(),
        custom_extensions: playback_capabilities.custom_extensions.clone(),
    }
}

pub fn clone_size(size: &Size) -> Size {
    Size { width: size.width, height: size.height }
}

pub fn clone_media_image(media_image: &MediaImage) -> MediaImage {
    MediaImage {
        image_type: media_image.image_type.clone(),
        url: media_image.url.clone(),
        mime_type: media_image.mime_type.clone(),
        sizes: media_image.sizes.iter().map(clone_size).collect(),
    }
}

impl Clone for Clonable<SessionEvent> {
    fn clone(&self) -> Self {
        Clonable(clone_session_event(&self.0))
    }
}

pub fn clone_session_event(event: &SessionEvent) -> SessionEvent {
    match event {
        SessionEvent::OnPlaybackStatusChanged { playback_status } => {
            SessionEvent::OnPlaybackStatusChanged {
                playback_status: clone_playback_status(&playback_status),
            }
        }
        SessionEvent::OnMetadataChanged { media_metadata } => {
            SessionEvent::OnMetadataChanged { media_metadata: clone_metadata(&media_metadata) }
        }
        SessionEvent::OnPlaybackCapabilitiesChanged { playback_capabilities } => {
            SessionEvent::OnPlaybackCapabilitiesChanged {
                playback_capabilities: clone_playback_capabilities(&playback_capabilities),
            }
        }
        SessionEvent::OnMediaImagesChanged { media_images } => SessionEvent::OnMediaImagesChanged {
            media_images: media_images.iter().map(clone_media_image).collect(),
        },
    }
}
