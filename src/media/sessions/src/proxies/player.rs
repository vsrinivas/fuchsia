// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    id::Id,
    services::discovery::{filter::*, player_event::PlayerEvent},
    Result,
};
use anyhow::Error as FError;
use fidl::endpoints::ClientEnd;
use fidl::{self, client::QueryResponseFut};
use fidl_fuchsia_media::*;
use fidl_fuchsia_media_sessions2::*;
use fidl_table_validation::*;
use fuchsia_inspect as inspect;
use fuchsia_syslog::{fx_log_info, fx_log_warn};
use futures::{
    future::{BoxFuture, FutureExt},
    stream::{FusedStream, FuturesUnordered},
    Future, Stream, StreamExt, TryStreamExt,
};
use inspect::Property;
use std::{
    convert::*,
    pin::Pin,
    task::{Context, Poll, Waker},
};

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
    id: Id,
    inner: PlayerProxy,
    state: ValidPlayerInfoDelta,
    registration: ValidPlayerRegistration,
    hanging_get: Option<QueryResponseFut<PlayerInfoDelta>>,
    terminated: bool,
    // TODO(41131): Use structured data when a proc macro to derive
    // Inspect support is available.
    inspect_handle: inspect::StringProperty,
    control_tasks: FuturesUnordered<BoxFuture<'static, ()>>,
    waker: Option<Waker>,
}

impl Player {
    pub fn new(
        id: Id,
        client_end: ClientEnd<PlayerMarker>,
        registration: PlayerRegistration,
        inspect_handle: inspect::StringProperty,
    ) -> Result<Self> {
        Ok(Player {
            id,
            inner: client_end.into_proxy()?,
            state: ValidPlayerInfoDelta::default(),
            registration: ValidPlayerRegistration::try_from(registration)?,
            hanging_get: None,
            terminated: false,
            inspect_handle,
            control_tasks: FuturesUnordered::new(),
            waker: None,
        })
    }

    pub fn id(&self) -> u64 {
        self.id.get()
    }

    /// Updates state with the latest delta published by the player.
    pub fn update(&mut self, delta: ValidPlayerInfoDelta) {
        self.state = ValidPlayerInfoDelta::apply(self.state.clone(), delta);
        self.inspect_handle.set(&format!("{:#?}", self.state));
    }

    /// Spawns a task to serve requests from `fuchsia.media.sessions2.SessionControl` with the
    /// backing `fuchsia.media.sessions2.Player` protocol.
    ///
    /// Drops the `SessionControl` channel if the backing player is disconnected at the time of a
    /// request, or if `disconnect_proxied_clients` is called.
    pub fn serve_controls(
        &mut self,
        requests: SessionControlRequestStream,
        mut recv: impl FusedStream + Stream<Item = SessionInfoDelta> + Send + Unpin + 'static,
    ) {
        let proxy = self.inner.clone();

        let mut requests = requests.map_err(FError::from).fuse();
        let mut status = None;
        let mut hanging_get = None;

        let control_task = async move {
            loop {
                futures::select! {
                    request = requests.select_next_some() => {
                        // Type inference fails dramatically here. Hand-holding required.
                        let request = match request {
                            Ok(request) => request,
                            Err(e) => {
                                let e: FError = e;
                                return Err(e);
                            }
                        };
                        match request {
                            SessionControlRequest::Play { .. } => proxy.play()?,
                            SessionControlRequest::Pause { .. } => proxy.pause()?,
                            SessionControlRequest::Stop { .. } => proxy.stop()?,
                            SessionControlRequest::Seek { position, .. } => {
                                proxy.seek(position)?
                            }
                            SessionControlRequest::SkipForward { .. } => proxy.skip_forward()?,
                            SessionControlRequest::SkipReverse { .. } => proxy.skip_reverse()?,
                            SessionControlRequest::NextItem { .. } => proxy.next_item()?,
                            SessionControlRequest::PrevItem { .. } => proxy.prev_item()?,
                            SessionControlRequest::SetPlaybackRate {
                                playback_rate, ..
                            } => proxy.set_playback_rate(playback_rate)?,
                            SessionControlRequest::SetRepeatMode { repeat_mode, .. } => {
                                proxy.set_repeat_mode(repeat_mode)?
                            }
                            SessionControlRequest::SetShuffleMode { shuffle_on, .. } => {
                                proxy.set_shuffle_mode(shuffle_on)?
                            }
                            SessionControlRequest::BindVolumeControl {
                                volume_control_request,
                                ..
                            } => proxy.bind_volume_control(volume_control_request)?,
                            SessionControlRequest::WatchStatus { responder } => {
                                if hanging_get.is_some() {
                                    fx_log_warn!(
                                        tag: "player_proxy",
                                        "Session observer sent duplicate watch"
                                    );
                                    // Close the channel.
                                    return Ok(());
                                }

                                if let Some(status) = status.take() {
                                    responder.send(status)?;
                                } else {
                                    hanging_get = Some(responder);
                                }
                            }
                        }
                    }
                    update = recv.select_next_some() => {
                        if let Some(hanging_get) = hanging_get.take() {
                            hanging_get.send(update)?;
                        } else {
                            status = Some(update);
                        }
                    }
                    complete => {
                        return Ok(());
                    },
                }
            }
        };

        self.control_tasks.push(control_task.map(drop).boxed());
        self.waker.iter().for_each(Waker::wake_by_ref);
    }

    fn options_satisfied(&self) -> WatchOptions {
        WatchOptions {
            only_active: Some(self.state.is_active().unwrap_or(false)),
            allowed_sessions: Some(vec![self.id.get()]),
        }
    }
}

/// The Stream implementation for Player is a stream of full player states. A new state is emitted
/// when the backing player implementation sends us an update.
impl Stream for Player {
    type Item = FilterApplicant<(u64, PlayerEvent)>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if self.terminated {
            return Poll::Ready(None);
        }

        // Send any queued messages to the player.
        self.waker = Some(cx.waker().clone());
        let _ = Pin::new(&mut self.control_tasks).poll_next(cx);

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
                Poll::Ready(Some(FilterApplicant::new(
                    self.options_satisfied(),
                    (self.id.get(), event),
                )))
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
    use fuchsia_async as fasync;
    use futures::{
        channel::mpsc::channel,
        future,
        sink::SinkExt,
        stream::{self, StreamExt},
    };
    use futures_test::task::noop_waker;
    use inspect::{assert_inspect_tree, Inspector};
    use test_util::assert_matches;

    static TEST_DOMAIN: &str = "test_domain";

    fn test_player() -> (Inspector, Player, ServerEnd<PlayerMarker>) {
        let (player_client, player_server) =
            create_endpoints::<PlayerMarker>().expect("Creating endpoints for test");
        let inspector = Inspector::new();
        let player = Player::new(
            Id::new().expect("Creating id for test player"),
            player_client,
            PlayerRegistration { domain: Some(TEST_DOMAIN.to_string()), ..Decodable::new_empty() },
            inspector.root().create_string("test_player", ""),
        )
        .expect("Creating player from valid prereqs");
        (inspector, player, player_server)
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn clients_waits_for_new_status() -> Result<()> {
        let (_inspector, mut player, _player_server) = test_player();

        let (session_control_client, session_control_server) =
            create_endpoints::<SessionControlMarker>()?;
        let session_control_fidl_proxy: SessionControlProxy =
            session_control_client.into_proxy()?;
        let session_control_request_stream = session_control_server.into_stream()?;

        let (mut sender, receiver) = channel(1);
        player.serve_controls(session_control_request_stream, receiver);
        fasync::spawn(async move { while let Some(_) = player.next().await {} });

        let waker = noop_waker();
        let mut ctx = Context::from_waker(&waker);
        let mut status_fut = session_control_fidl_proxy.watch_status();
        let poll_result = Pin::new(&mut status_fut).poll(&mut ctx);
        assert_matches!(poll_result, Poll::Pending);

        sender.send(SessionInfoDelta::new_empty()).await?;
        let result = status_fut.await;
        assert_matches!(result, Ok(_));

        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn client_gets_cached_player_status() -> Result<()> {
        let (_inspector, mut player, _player_server) = test_player();

        let (session_control_client, session_control_server) =
            create_endpoints::<SessionControlMarker>()?;
        let session_control_fidl_proxy: SessionControlProxy =
            session_control_client.into_proxy()?;
        let session_control_request_stream = session_control_server.into_stream()?;

        player.serve_controls(
            session_control_request_stream,
            stream::once(future::ready(SessionInfoDelta::new_empty())),
        );

        fasync::spawn(async move { while let Some(_) = player.next().await {} });

        assert_matches!(session_control_fidl_proxy.watch_status().await, Ok(_));

        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn client_channel_closes_when_backing_player_disconnects() -> Result<()> {
        let (_inspector, mut player, _player_server) = test_player();

        let (session_control_client, session_control_server) =
            create_endpoints::<SessionControlMarker>()?;
        let session_control_fidl_proxy: SessionControlProxy =
            session_control_client.into_proxy()?;
        let session_control_request_stream = session_control_server.into_stream()?;

        player.serve_controls(session_control_request_stream, stream::empty());

        assert!(session_control_fidl_proxy.play().is_ok());

        drop(player);

        assert!(session_control_fidl_proxy.pause().is_err());

        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn update_stream_relays_player_state() -> Result<()> {
        let (_inspector, mut player, player_server) = test_player();
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
        let (_inspector, mut player, _) = test_player();
        let mut player_stream = Pin::new(&mut player);
        let (_, event) = player_stream.next().await.expect("Polling player event").applicant;
        assert_matches!(event, PlayerEvent::Removed);
        assert!(player_stream.is_terminated());
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn update_stream_terminates_on_invalid_delta() -> Result<()> {
        let (_inspector, mut player, player_server) = test_player();
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

    #[fasync::run_singlethreaded]
    #[test]
    #[allow(unused)]
    async fn inspect_node() -> Result<()> {
        let (inspector, mut player, player_server) = test_player();
        let mut requests = player_server.into_stream()?;

        assert_inspect_tree!(inspector, root: {
            test_player: "",
        });

        let waker = noop_waker();
        let mut ctx = Context::from_waker(&waker);

        let delta = PlayerInfoDelta {
            player_capabilities: Some(PlayerCapabilities {
                flags: Some(PlayerCapabilityFlags::Play | PlayerCapabilityFlags::Pause),
            }),
            ..Decodable::new_empty()
        };

        // Poll the stream so that it sends a watch request to the backing player.
        let poll_result = Pin::new(&mut player).poll_next(&mut ctx);
        assert_matches!(poll_result, Poll::Pending);

        let info_change_responder = requests
            .try_next()
            .await?
            .expect("Receiving a request")
            .into_watch_info_change()
            .expect("Receiving info change responder");
        info_change_responder.send(delta)?;

        let _ = player.next().await.expect("Polling player event");

        const EXPECTED: &'static str = r"ValidPlayerInfoDelta {
    local: None,
    player_status: None,
    metadata: None,
    media_images: None,
    player_capabilities: Some(
        ValidPlayerCapabilities {
            flags: Play | Pause,
        },
    ),
}";

        assert_inspect_tree!(inspector, root: {
            test_player: EXPECTED,
        });

        Ok(())
    }
}
