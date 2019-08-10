// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{wait_group::*, Result};
use failure::Error as FError;
use fidl_fuchsia_media::*;
use fidl_fuchsia_media_sessions2::*;
use fidl_table_validation::*;
use fuchsia_async as fasync;
use futures::{
    future::{AbortHandle, Abortable},
    prelude::*,
};
use std::convert::*;

#[derive(Debug, Clone, ValidFidlTable)]
#[fidl_table_src(PlayerStatus)]
pub struct ValidPlayerStatus {
    #[fidl_field_type(optional)]
    pub duration: Option<i64>,
    pub player_state: PlayerState,
    #[fidl_field_type(optional)]
    pub timeline_function: Option<TimelineFunction>,
    pub repeat_mode: RepeatMode,
    pub shuffle_on: bool,
    pub content_type: ContentType,
    #[fidl_field_type(optional)]
    pub error: Option<Error>,
}

#[derive(Debug, Clone, ValidFidlTable)]
#[fidl_table_src(PlayerCapabilities)]
pub struct ValidPlayerCapabilities {
    pub flags: PlayerCapabilityFlags,
}

#[derive(Debug, Clone, ValidFidlTable)]
#[fidl_table_src(MediaImage)]
#[fidl_table_validator(MediaImageValidator)]
pub struct ValidMediaImage {
    pub image_type: MediaImageType,
    pub sizes: Vec<ImageSizeVariant>,
}

#[derive(Debug)]
pub enum MediaImageError {
    NoSizesEnumerated,
}

pub struct MediaImageValidator;

impl Validate<ValidMediaImage> for MediaImageValidator {
    type Error = MediaImageError;
    fn validate(candidate: &ValidMediaImage) -> std::result::Result<(), Self::Error> {
        if candidate.sizes.len() == 0 {
            return Err(MediaImageError::NoSizesEnumerated);
        }

        Ok(())
    }
}

#[derive(Debug, Clone, Default)]
pub struct ValidPlayerInfoDelta {
    pub local: Option<bool>,
    pub player_status: Option<ValidPlayerStatus>,
    pub metadata: Option<Metadata>,
    pub media_images: Option<Vec<ValidMediaImage>>,
    pub player_capabilities: Option<ValidPlayerCapabilities>,
}

// TODO(turnage): Fix TryFrom path for optional fields in ValidFidlTable, so this
// manual impl can be removed.
impl TryFrom<PlayerInfoDelta> for ValidPlayerInfoDelta {
    type Error = FError;
    fn try_from(src: PlayerInfoDelta) -> std::result::Result<Self, FError> {
        Ok(Self {
            local: src.local,
            player_status: src.player_status.map(ValidPlayerStatus::try_from).transpose()?,
            metadata: src.metadata,
            media_images: src
                .media_images
                .map(|images| {
                    images
                        .into_iter()
                        .map(ValidMediaImage::try_from)
                        .collect::<std::result::Result<Vec<ValidMediaImage>, _>>()
                })
                .transpose()?,
            player_capabilities: src
                .player_capabilities
                .map(ValidPlayerCapabilities::try_from)
                .transpose()?,
        })
    }
}

impl ValidPlayerInfoDelta {
    /// Update this delta with a newer delta, overwriting now-out-of-date information.
    pub fn apply(self, delta: ValidPlayerInfoDelta) -> Self {
        Self {
            local: delta.local.or(self.local),
            player_status: delta.player_status.or(self.player_status),
            metadata: delta.metadata.or(self.metadata),
            media_images: delta.media_images.or(self.media_images),
            player_capabilities: delta.player_capabilities.or(self.player_capabilities),
        }
    }

    /// Returns whether the state represented by this delta is active.
    pub fn is_active(&self) -> Option<bool> {
        self.player_status
            .as_ref()
            .map(|status| status.player_state)
            .map(|state| state == PlayerState::Playing)
    }
}

/// A proxy for published `fuchsia.media.sessions2.Player` protocols.
#[derive(Debug)]
pub struct Player {
    inner: PlayerProxy,
    state: ValidPlayerInfoDelta,
    server_handles: Vec<AbortHandle>,
    server_wait_group: WaitGroup,
}

impl Player {
    pub fn new(proxy: PlayerProxy) -> Self {
        Player {
            inner: proxy,
            state: ValidPlayerInfoDelta::default(),
            server_handles: vec![],
            server_wait_group: WaitGroup::new(),
        }
    }

    /// Sends the player a request for status and returns a future that will resolve when the player
    /// replies.
    pub fn poll(&self, id: u64) -> impl Future<Output = (u64, Result<ValidPlayerInfoDelta>)> {
        let proxy = self.inner.clone();
        async move {
            (
                id,
                proxy.watch_info_change().await
                    .map_err(Into::into)
                    .and_then(|delta| ValidPlayerInfoDelta::try_from(delta).map_err(Into::into)),
            )
        }
    }

    /// Updates state with the latest delta published by the player.
    pub fn update(&mut self, delta: ValidPlayerInfoDelta) {
        self.state = ValidPlayerInfoDelta::apply(self.state.clone(), delta);
    }

    pub fn state(&self) -> &ValidPlayerInfoDelta {
        &self.state
    }

    /// Spawns a task to serve requests from `fuchsia.media.sessions2.SessionControl` with the
    /// backing `fuchsia.media.sessions2.Player` protocol.
    ///
    /// Drops the `SessionControl` channel if the backing player is disconnected at the time of a
    /// request, or if `disconnect_proxied_clients` is called.
    pub fn serve_controls(&mut self, mut requests: SessionControlRequestStream) {
        let proxy = self.inner.clone();

        let (abort_handle, abort_registration) = AbortHandle::new_pair();
        self.server_handles.push(abort_handle);

        let waiter = self.server_wait_group.new_waiter();

        fasync::spawn_local(
            Abortable::new(
                async move {
                    while let Ok(Some(Ok(_))) = requests.try_next().await.map(|r| {
                        r.map(|r| match r {
                            SessionControlRequest::Play { .. } => proxy.play(),
                            SessionControlRequest::Pause { .. } => proxy.pause(),
                            SessionControlRequest::Stop { .. } => proxy.stop(),
                            SessionControlRequest::Seek { position, .. } => proxy.seek(position),
                            SessionControlRequest::SkipForward { .. } => proxy.skip_forward(),
                            SessionControlRequest::SkipReverse { .. } => proxy.skip_reverse(),
                            SessionControlRequest::NextItem { .. } => proxy.next_item(),
                            SessionControlRequest::PrevItem { .. } => proxy.prev_item(),
                            SessionControlRequest::SetPlaybackRate { playback_rate, .. } => {
                                proxy.set_playback_rate(playback_rate)
                            }
                            SessionControlRequest::SetRepeatMode { repeat_mode, .. } => {
                                proxy.set_repeat_mode(repeat_mode)
                            }
                            SessionControlRequest::SetShuffleMode { shuffle_on, .. } => {
                                proxy.set_shuffle_mode(shuffle_on)
                            }
                            SessionControlRequest::BindGainControl {
                                gain_control_request, ..
                            } => proxy.bind_gain_control(gain_control_request),
                        })
                    }) {}
                    drop(waiter);
                },
                abort_registration,
            )
            .map(|_| ()),
        );
    }

    /// Disconnects all proxied clients. Returns when all clients are disconnected.
    pub async fn disconnect_proxied_clients(&mut self) {
        for server in self.server_handles.drain(0..) {
            server.abort();
        }
        self.server_wait_group.wait().await;
    }
}

#[cfg(test)]
mod test {
    use super::*;

    #[fasync::run_singlethreaded]
    #[test]
    async fn stream_ends_when_backing_player_disconnects() -> Result<()> {
        use fidl::endpoints::*;

        let (player_client, _player_server) = create_endpoints::<PlayerMarker>()?;
        let player_fidl_proxy = player_client.into_proxy()?;
        let mut player = Player::new(player_fidl_proxy);

        let (session_control_client, session_control_server) =
            create_endpoints::<SessionControlMarker>()?;
        let session_control_fidl_proxy: SessionControlProxy =
            session_control_client.into_proxy()?;
        let session_control_request_stream = session_control_server.into_stream()?;

        player.serve_controls(session_control_request_stream);

        assert!(session_control_fidl_proxy.play().is_ok());

        player.disconnect_proxied_clients().await;

        assert!(session_control_fidl_proxy.pause().is_err());

        Ok(())
    }
}
