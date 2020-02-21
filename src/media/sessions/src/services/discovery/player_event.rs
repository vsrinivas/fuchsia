// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::proxies::player::*;
use fidl::encoding::Decodable;
use fidl_fuchsia_media_sessions2::*;

#[derive(Debug, Clone, PartialEq)]
/// A change to the state of a published media player.
pub enum PlayerEvent {
    Updated {
        delta: ValidPlayerInfoDelta,
        registration: Option<ValidPlayerRegistration>,
        active: Option<bool>,
    },
    Removed,
}

impl Default for PlayerEvent {
    fn default() -> Self {
        PlayerEvent::Updated {
            delta: ValidPlayerInfoDelta::default(),
            registration: None,
            active: None,
        }
    }
}

#[derive(Debug)]
pub enum SessionsWatcherEvent {
    Updated(SessionInfoDelta),
    Removed,
}

impl PlayerEvent {
    /// Renders the event for the client as a `SessionsWatcher` message.
    pub fn sessions_watcher_event(self) -> SessionsWatcherEvent {
        match self {
            PlayerEvent::Updated { delta, active, registration } => {
                SessionsWatcherEvent::Updated(SessionInfoDelta {
                    is_locally_active: active,
                    domain: registration.map(|r| r.domain),
                    is_local: delta.local,
                    player_status: delta.player_status.map(|player_status| PlayerStatus {
                        duration: player_status.duration,
                        player_state: Some(player_status.player_state),
                        timeline_function: player_status.timeline_function,
                        repeat_mode: Some(player_status.repeat_mode),
                        shuffle_on: Some(player_status.shuffle_on),
                        content_type: Some(player_status.content_type),
                        error: player_status.error,
                        ..Decodable::new_empty()
                    }),
                    metadata: delta.metadata,
                    player_capabilities: Some(PlayerCapabilities {
                        flags: delta.player_capabilities.map(|c| c.flags),
                    }),
                    media_images: delta.media_images.map(|media_images| {
                        media_images
                            .into_iter()
                            .map(|media_image| MediaImage {
                                image_type: Some(media_image.image_type),
                                sizes: Some(media_image.sizes),
                            })
                            .collect()
                    }),
                    ..Decodable::new_empty()
                })
            }
            PlayerEvent::Removed => SessionsWatcherEvent::Removed,
        }
    }
}
