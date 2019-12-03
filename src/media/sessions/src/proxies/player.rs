// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    services::discovery::{filter::*, player_event::PlayerEvent},
    Result,
};
use failure::Error as FError;
use fidl::client::QueryResponseFut;
use fidl::endpoints::ClientEnd;
use fidl_fuchsia_media::*;
use fidl_fuchsia_media_sessions2::*;
use fidl_table_validation::*;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_info;
use fuchsia_zircon::{self as zx, AsHandleRef};
use futures::{
    future::{AbortHandle, Abortable},
    stream::FusedStream,
    task::{Context, Poll},
    Future, FutureExt, Stream, TryStreamExt,
};
use std::convert::*;
use std::pin::Pin;
use waitgroup::*;

#[derive(Debug, Clone, ValidFidlTable, PartialEq)]
#[fidl_table_src(PlayerRegistration)]
pub struct ValidPlayerRegistration {
    pub domain: String,
}

#[derive(Debug, Clone, ValidFidlTable, PartialEq)]
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

#[derive(Debug, Clone, ValidFidlTable, PartialEq)]
#[fidl_table_src(PlayerCapabilities)]
pub struct ValidPlayerCapabilities {
    pub flags: PlayerCapabilityFlags,
}

#[derive(Debug, Clone, ValidFidlTable, PartialEq)]
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

#[derive(Debug, Clone, Default, PartialEq)]
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
    id: u64,
    id_handle: zx::Event,
    inner: PlayerProxy,
    state: ValidPlayerInfoDelta,
    server_handles: Vec<AbortHandle>,
    server_wait_group: WaitGroup,
    registration: ValidPlayerRegistration,
    hanging_get: Option<QueryResponseFut<PlayerInfoDelta>>,
    terminated: bool,
}

impl Player {
    pub fn new(
        client_end: ClientEnd<PlayerMarker>,
        registration: PlayerRegistration,
    ) -> Result<Self> {
        let id_handle = zx::Event::create()?;
        let id = id_handle.get_koid()?.raw_koid();
        Ok(Player {
            id,
            id_handle,
            inner: client_end.into_proxy()?,
            state: ValidPlayerInfoDelta::default(),
            server_handles: vec![],
            server_wait_group: WaitGroup::new(),
            registration: ValidPlayerRegistration::try_from(registration)?,
            hanging_get: None,
            terminated: false,
        })
    }

    pub fn id(&self) -> u64 {
        self.id
    }

    /// Updates state with the latest delta published by the player.
    pub fn update(&mut self, delta: ValidPlayerInfoDelta) {
        self.state = ValidPlayerInfoDelta::apply(self.state.clone(), delta);
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
                            SessionControlRequest::BindVolumeControl {
                                volume_control_request,
                                ..
                            } => proxy.bind_volume_control(volume_control_request),
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
    pub fn disconnect_proxied_clients<'a>(&'a mut self) -> impl Future<Output = ()> + 'a {
        for server in self.server_handles.drain(0..) {
            server.abort();
        }
        self.server_wait_group.wait()
    }

    fn options_satisfied(&self) -> WatchOptions {
        WatchOptions {
            only_active: Some(self.state.is_active().unwrap_or(false)),
            allowed_sessions: Some(vec![self.id]),
        }
    }
}

/// The Stream implementation for Player is a stream of full player states. A new state is emitted
/// when the backing player implementation sends us an update.
impl Stream for Player {
    type Item = FilterApplicant<(u64, PlayerEvent)>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        let proxy = self.inner.clone();
        let hanging_get = self.hanging_get.get_or_insert_with(move || proxy.watch_info_change());

        match Pin::new(hanging_get).poll(cx) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(r) => {
                let event = match r {
                    Err(e) => match e {
                        fidl::Error::ClientRead(_) | fidl::Error::ClientWrite(_) => {
                            PlayerEvent::Removed
                        }
                        e => {
                            fx_log_info!(tag: "player_proxy", "Player update failed: {:#?}", e);
                            PlayerEvent::Removed
                        }
                    },
                    Ok(delta) => match ValidPlayerInfoDelta::try_from(delta) {
                        Ok(delta) => {
                            self.update(delta);
                            self.hanging_get = None;
                            PlayerEvent::Updated {
                                delta: self.state.clone(),
                                registration: Some(self.registration.clone()),
                                active: self.state.is_active(),
                            }
                        }
                        Err(e) => {
                            fx_log_info!(
                                tag: "player_proxy",
                                "Player sent invalid update: {:#?}", e);
                            PlayerEvent::Removed
                        }
                    },
                };
                if let PlayerEvent::Removed = event {
                    self.terminated = true;
                }
                Poll::Ready(Some(FilterApplicant::new(self.options_satisfied(), (self.id, event))))
            }
        }
    }
}

impl FusedStream for Player {
    fn is_terminated(&self) -> bool {
        self.terminated
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use fidl::encoding::Decodable;
    use fidl::endpoints::*;
    use futures::stream::StreamExt;
    use futures_test::task::noop_waker;
    use test_util::assert_matches;

    static TEST_DOMAIN: &str = "test_domain";

    fn test_player() -> (Player, ServerEnd<PlayerMarker>) {
        let (player_client, player_server) =
            create_endpoints::<PlayerMarker>().expect("Creating endpoints for test");
        let player = Player::new(
            player_client,
            PlayerRegistration { domain: Some(TEST_DOMAIN.to_string()) },
        )
        .expect("Creating player from valid prereqs");
        (player, player_server)
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn client_channel_closes_when_backing_player_disconnects() -> Result<()> {
        let (mut player, _player_server) = test_player();

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

    #[fasync::run_singlethreaded]
    #[test]
    async fn update_stream_relays_player_state() -> Result<()> {
        let (mut player, player_server) = test_player();
        let mut requests = player_server.into_stream()?;

        let waker = noop_waker();
        let mut ctx = Context::from_waker(&waker);

        // Poll the stream so that it sends a watch request to the backing player.
        let poll_result = Pin::new(&mut player).poll_next(&mut ctx);
        assert_matches!(poll_result, Poll::Pending);

        let info_change_responder = requests
            .try_next()
            .await?
            .expect("Receiving a request")
            .into_watch_info_change()
            .expect("Receiving info change responder");
        info_change_responder.send(Decodable::new_empty())?;

        let mut player_stream = Pin::new(&mut player);
        let (_, event) = player_stream.next().await.expect("Polling player event").applicant;
        assert_eq!(
            event,
            PlayerEvent::Updated {
                delta: ValidPlayerInfoDelta::default(),
                registration: Some(ValidPlayerRegistration { domain: TEST_DOMAIN.to_string() }),
                active: None,
            }
        );
        assert!(!player_stream.is_terminated());

        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn update_stream_terminates_when_backing_player_disconnects() {
        let (mut player, _) = test_player();
        let mut player_stream = Pin::new(&mut player);
        let (_, event) = player_stream.next().await.expect("Polling player event").applicant;
        assert_matches!(event, PlayerEvent::Removed);
        assert!(player_stream.is_terminated());
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn update_stream_terminates_on_invalid_delta() -> Result<()> {
        let (mut player, player_server) = test_player();
        let mut requests = player_server.into_stream()?;

        let waker = noop_waker();
        let mut ctx = Context::from_waker(&waker);

        // Poll the stream so that it sends a watch request to the backing player.
        let poll_result = Pin::new(&mut player).poll_next(&mut ctx);
        assert_matches!(poll_result, Poll::Pending);

        let info_change_responder = requests
            .try_next()
            .await?
            .expect("Receiving a request")
            .into_watch_info_change()
            .expect("Receiving info change responder");
        info_change_responder.send(PlayerInfoDelta {
            player_capabilities: Some(Decodable::new_empty()),
            ..Decodable::new_empty()
        })?;

        let mut player_stream = Pin::new(&mut player);
        let (_, event) = player_stream.next().await.expect("Polling player event").applicant;
        assert_matches!(event, PlayerEvent::Removed);
        assert!(player_stream.is_terminated());

        Ok(())
    }
}
