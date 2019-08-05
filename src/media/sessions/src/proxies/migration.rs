// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! This module contains a proxy from players published via the old API to the new API.

// TODO(turnage): Remove file after migrating clients to sessions2.

use crate::{
    mpmc, proxies::session::*, services::publisher2::*, spawn_log_error,
    state::session_list::SessionList, Ref, Result,
};
use fidl::encoding::Decodable;
use fidl::endpoints::{create_endpoints, ServerEnd};
use fidl_fuchsia_media_sessions as sesh;
use fidl_fuchsia_media_sessions2 as sesh2;
use futures::{channel::mpsc, future, prelude::*};
use std::collections::HashMap;

const MIGRATED_DOMAIN: &str = "domain://MIGRATED_FROM_V1";

pub async fn migrator(
    session_list: Ref<SessionList>,
    mut collection_event_stream: mpmc::Receiver<(SessionRegistration, SessionCollectionEvent)>,
    mut player_sink: mpsc::Sender<NewPlayer>,
) -> Result<()> {
    let mut proxies = HashMap::new();

    while let Some((registration, event)) = collection_event_stream.next().await {
        match event {
            SessionCollectionEvent::Added => {
                let (session_client, session_server) = create_endpoints()?;
                match session_list.lock().await.get(registration.koid) {
                    Some(session) => session.connect(session_server).await?,
                    None => continue,
                };

                let (player_client, player_server) = create_endpoints()?;
                player_sink
                    .send(NewPlayer {
                        proxy: player_client.into_proxy()?,
                        registration: ValidPlayerRegistration {
                            domain: MIGRATED_DOMAIN.to_string(),
                        },
                    })
                    .await?;

                let handler = migrate_session_to_player(
                    registration.is_local,
                    session_client.into_proxy()?,
                    player_server,
                );
                let (handler, abort_handle) = future::abortable(handler);
                proxies.insert(registration.koid, abort_handle);
                spawn_log_error(handler.map(|r| {
                    match r {
                        Ok(r) => r,
                        Err(_) => Ok(()), // Aborted due to session dropping; this is ok.
                    }
                }));
            }
            SessionCollectionEvent::Removed => {
                if let Some(abort_handle) = proxies.remove(&registration.koid) {
                    abort_handle.abort();
                }
            }
        }
    }

    Ok(())
}

async fn migrate_session_to_player(
    local: bool,
    session: sesh::SessionProxy,
    player_server: ServerEnd<sesh2::PlayerMarker>,
) -> Result<()> {
    let mut events = session.take_event_stream();
    let mut player_requests = player_server.into_stream()?;
    let mut hanging_get: Option<sesh2::PlayerWatchInfoChangeResponder> = None;
    let mut staged_event =
        Some(sesh2::PlayerInfoDelta { local: Some(local), ..Decodable::new_empty() });

    loop {
        futures::select! {
            event = events.try_next() => {
                let event = if let Some(event) = event? {
                    event
                } else {
                    // The backing player has disconnected; drop the proxy.
                    return Ok(());
                };

                let delta = if let Some(delta) = event.migrate() {
                    delta
                } else {
                    continue;
                };

                let staged = staged_event.take().unwrap_or_else(Decodable::new_empty);
                let staged = apply_delta_to(staged, delta);

                if let Some(responder) = hanging_get.take() {
                    responder.send(staged)?;
                } else {
                    staged_event = Some(staged);
                }
            },
            // The service will never close this.
            request = player_requests.select_next_some() => {
                match request? {
                    sesh2::PlayerRequest::Play { .. } => {
                        session.play()?;
                    }
                    sesh2::PlayerRequest::Pause { .. } => session.pause()?,
                    sesh2::PlayerRequest::Stop { .. } => session.stop()?,
                    sesh2::PlayerRequest::Seek { position, .. } => {
                        session.seek_to_position(position)?
                    }
                    sesh2::PlayerRequest::SkipForward { .. } => session.skip_forward(0)?,
                    sesh2::PlayerRequest::SkipReverse { .. } => session.skip_reverse(0)?,
                    sesh2::PlayerRequest::NextItem { .. } => session.next_item()?,
                    sesh2::PlayerRequest::PrevItem { .. } => session.prev_item()?,
                    sesh2::PlayerRequest::SetPlaybackRate { playback_rate, .. } => {
                        session.set_playback_rate(playback_rate)?
                    }
                    sesh2::PlayerRequest::SetRepeatMode { repeat_mode, .. } => {
                        session.set_repeat_mode(match repeat_mode {
                            sesh2::RepeatMode::Off => sesh::RepeatMode::Off,
                            sesh2::RepeatMode::Group => sesh::RepeatMode::Group,
                            sesh2::RepeatMode::Single => sesh::RepeatMode::Single,
                        })?
                    }
                    sesh2::PlayerRequest::SetShuffleMode { shuffle_on, .. } => {
                        session.set_shuffle_mode(shuffle_on)?
                    }
                    sesh2::PlayerRequest::BindGainControl { gain_control_request, .. } => {
                        session.bind_gain_control(gain_control_request)?
                    }
                    sesh2::PlayerRequest::WatchInfoChange { responder } => {
                        if hanging_get.is_some() {
                            fuchsia_syslog::fx_log_info!(
                                tag: "mediasession",
                                "Illegal concurrent watch");
                            return Ok(());
                        }

                        if let Some(staged) = staged_event.take() {
                            responder.send(staged)?;
                        } else {
                            hanging_get = Some(responder);
                        }
                    }
                };
            }
        }
    }
}

fn apply_delta_to(
    info: sesh2::PlayerInfoDelta,
    delta: sesh2::PlayerInfoDelta,
) -> sesh2::PlayerInfoDelta {
    sesh2::PlayerInfoDelta {
        local: delta.local.or(info.local),
        player_status: delta.player_status.or(info.player_status),
        metadata: delta.metadata.or(info.metadata),
        media_images: delta.media_images.or(info.media_images),
        player_capabilities: delta.player_capabilities.or(info.player_capabilities),
    }
}

trait Migrate {
    type NewType;
    fn migrate(self) -> Self::NewType;
}

impl Migrate for sesh::SessionEvent {
    type NewType = Option<sesh2::PlayerInfoDelta>;
    fn migrate(self) -> Self::NewType {
        match self {
            sesh::SessionEvent::OnPlaybackStatusChanged { playback_status } => {
                Some(sesh2::PlayerInfoDelta {
                    player_status: Some(playback_status.migrate()),
                    ..Decodable::new_empty()
                })
            }
            sesh::SessionEvent::OnMetadataChanged { media_metadata } => {
                Some(sesh2::PlayerInfoDelta {
                    metadata: Some(media_metadata),
                    ..Decodable::new_empty()
                })
            }
            sesh::SessionEvent::OnMediaImagesChanged { media_images: _ } => {
                // TODO(turnage): Make a mapping for this or migrate everyone
                None
            }
            sesh::SessionEvent::OnPlaybackCapabilitiesChanged { playback_capabilities } => {
                Some(sesh2::PlayerInfoDelta {
                    player_capabilities: Some(playback_capabilities.migrate()),
                    ..Decodable::new_empty()
                })
            }
        }
    }
}

impl Migrate for sesh::PlaybackState {
    type NewType = sesh2::PlayerState;
    fn migrate(self) -> Self::NewType {
        match self {
            sesh::PlaybackState::Stopped => sesh2::PlayerState::Idle,
            sesh::PlaybackState::Playing => sesh2::PlayerState::Playing,
            sesh::PlaybackState::Paused => sesh2::PlayerState::Paused,
            sesh::PlaybackState::Error => sesh2::PlayerState::Error,
        }
    }
}

impl Migrate for sesh::PlaybackStatus {
    type NewType = sesh2::PlayerStatus;
    fn migrate(self) -> Self::NewType {
        sesh2::PlayerStatus {
            duration: self.duration,
            player_state: self.playback_state.map(|state| state.migrate()),
            shuffle_on: self.shuffle_on,
            timeline_function: self.playback_function,
            repeat_mode: self.repeat_mode.map(|mode| match mode {
                sesh::RepeatMode::Off => sesh2::RepeatMode::Off,
                sesh::RepeatMode::Group => sesh2::RepeatMode::Group,
                sesh::RepeatMode::Single => sesh2::RepeatMode::Single,
            }),
            error: self.error.map(|_| sesh2::Error::Other),
            content_type: Some(sesh2::ContentType::Other),
        }
    }
}

impl Migrate for sesh::PlaybackCapabilities {
    type NewType = sesh2::PlayerCapabilities;
    fn migrate(self) -> Self::NewType {
        sesh2::PlayerCapabilities {
            flags: {
                let mut flags = sesh2::PlayerCapabilityFlags::empty();

                let supported_repeat_modes = self.supported_repeat_modes.unwrap_or(vec![]);

                if supported_repeat_modes.iter().find(|m| **m == sesh::RepeatMode::Group).is_some()
                {
                    flags |= sesh2::PlayerCapabilityFlags::RepeatGroups;
                }

                if supported_repeat_modes.iter().find(|m| **m == sesh::RepeatMode::Single).is_some()
                {
                    flags |= sesh2::PlayerCapabilityFlags::RepeatSingle;
                }

                let src_flags = self.flags.unwrap_or(sesh::PlaybackCapabilityFlags::empty());

                for (src_bit, target_bit) in vec![
                    (sesh::PlaybackCapabilityFlags::Play, sesh2::PlayerCapabilityFlags::Play),
                    (sesh::PlaybackCapabilityFlags::Pause, sesh2::PlayerCapabilityFlags::Pause),
                    (
                        sesh::PlaybackCapabilityFlags::SkipForward,
                        sesh2::PlayerCapabilityFlags::SkipForward,
                    ),
                    (
                        sesh::PlaybackCapabilityFlags::SkipReverse,
                        sesh2::PlayerCapabilityFlags::SkipReverse,
                    ),
                    (sesh::PlaybackCapabilityFlags::Shuffle, sesh2::PlayerCapabilityFlags::Shuffle),
                    (
                        sesh::PlaybackCapabilityFlags::SeekToPosition,
                        sesh2::PlayerCapabilityFlags::Seek,
                    ),
                    (
                        sesh::PlaybackCapabilityFlags::ChangeToNextItem,
                        sesh2::PlayerCapabilityFlags::ChangeToNextItem,
                    ),
                    (
                        sesh::PlaybackCapabilityFlags::ChangeToPrevItem,
                        sesh2::PlayerCapabilityFlags::ChangeToPrevItem,
                    ),
                    (
                        sesh::PlaybackCapabilityFlags::HasGainControl,
                        sesh2::PlayerCapabilityFlags::HasGainControl,
                    ),
                ] {
                    if src_flags.contains(src_bit) {
                        flags |= target_bit;
                    }
                }

                Some(flags)
            },
        }
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::mpmc;
    use fidl_fuchsia_media::{Metadata, Property};
    use fuchsia_async as fasync;
    use fuchsia_zircon as zx;
    use std::rc::Rc;
    use zx::AsHandleRef;

    #[fasync::run_singlethreaded]
    #[test]
    async fn it_works() -> Result<()> {
        let session_list = Ref::default();
        let mut collection_event_sink = mpmc::Sender::default();
        let (player_sink, mut player_stream) = mpsc::channel(1);
        let (session_client, session_server) = create_endpoints()?;
        let (mut session_requests, session_control) =
            session_server.into_stream_and_control_handle()?;

        spawn_log_error(migrator(
            session_list.clone(),
            collection_event_sink.new_receiver(),
            player_sink,
        ));

        let event = zx::Event::create()?;
        let koid = event.get_koid()?;
        let registration = SessionRegistration { id: Rc::new(event), koid: koid, is_local: true };
        session_list.lock().await.push(
            registration.clone(),
            Session::serve(
                session_client,
                registration.clone(),
                Ref::default(),
                session_list.clone(),
                collection_event_sink.clone(),
                mpmc::Sender::default(),
            )
            .await?,
        );
        collection_event_sink.send((registration.clone(), SessionCollectionEvent::Added)).await;

        let new_player = player_stream.next().await.expect("Unwrapping session's proxied player");
        assert_eq!(
            new_player.registration.domain, MIGRATED_DOMAIN,
            "Proxied sessions should use a migrated domain constant."
        );

        let info = new_player.proxy.watch_info_change().await?;
        assert_eq!(
            info,
            sesh2::PlayerInfoDelta { local: Some(true), ..Decodable::new_empty() },
            "The first delta should announce the session's locality."
        );

        new_player.proxy.play()?;
        let session_request =
            session_requests.try_next().await?.expect("Unwrapping session request");
        assert!(
            session_request.into_play().is_some(),
            "Requests should proxy to the backing session."
        );

        session_control.send_on_metadata_changed(&mut Metadata {
            properties: vec![Property {
                label: String::from("label"),
                value: String::from("value"),
            }],
        })?;
        let info = new_player.proxy.watch_info_change().await?;
        assert!(info.metadata.is_some(), "Change events should become info change responses.");

        session_control.send_on_playback_status_changed(sesh::PlaybackStatus {
            playback_state: Some(sesh::PlaybackState::Playing),
            ..Decodable::new_empty()
        })?;
        let info = new_player.proxy.watch_info_change().await?;
        assert_eq!(
            info.player_status,
            Some(sesh2::PlayerStatus {
                player_state: Some(sesh2::PlayerState::Playing),
                content_type: Some(sesh2::ContentType::Other),
                ..Decodable::new_empty()
            }),
            "Status change events should become info change responses."
        );

        drop(session_control);
        drop(session_requests);

        assert!(new_player.proxy.watch_info_change().await.is_err());

        Ok(())
    }
}
