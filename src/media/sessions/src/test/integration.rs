// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::Result;
use anyhow::Context as _;
use fidl::encoding::Decodable;
use fidl::endpoints::{create_endpoints, create_proxy};
use fidl_fuchsia_media_sessions2::*;
use fuchsia_async as fasync;
use fuchsia_component as comp;
use fuchsia_zircon::{self as zx, AsHandleRef};
use futures::stream::TryStreamExt;
use test_util::assert_matches;

const MEDIASESSION_URL: &str = "fuchsia-pkg://fuchsia.com/mediasession#meta/mediasession.cmx";

struct TestService {
    // This needs to stay alive to keep the service running.
    #[allow(unused)]
    app: comp::client::App,
    publisher: PublisherProxy,
    discovery: DiscoveryProxy,
}

impl TestService {
    fn new() -> Result<Self> {
        let launcher = comp::client::launcher().context("Connecting to launcher")?;
        let mediasession = comp::client::launch(&launcher, String::from(MEDIASESSION_URL), None)
            .context("Launching mediasession")?;

        let publisher = mediasession
            .connect_to_service::<PublisherMarker>()
            .context("Connecting to Publisher")?;
        let discovery = mediasession
            .connect_to_service::<DiscoveryMarker>()
            .context("Connecting to Discovery")?;

        Ok(Self { app: mediasession, publisher, discovery })
    }

    fn new_watcher(&self, watch_options: WatchOptions) -> Result<TestWatcher> {
        let (watcher_client, watcher_server) = create_endpoints()?;
        self.discovery.watch_sessions(watch_options, watcher_client)?;
        Ok(TestWatcher { watcher: watcher_server.into_stream()? })
    }
}

struct TestWatcher {
    watcher: SessionsWatcherRequestStream,
}

impl TestWatcher {
    async fn wait_for_n_updates(&mut self, n: usize) -> Result<Vec<(u64, SessionInfoDelta)>> {
        let mut updates: Vec<(u64, SessionInfoDelta)> = vec![];
        for i in 0..n {
            let (id, delta, responder) = self
                .watcher
                .try_next()
                .await?
                .and_then(|r| r.into_session_updated())
                .expect(&format!("Unwrapping watcher request {:?}", i));
            responder.send()?;
            updates.push((id, delta));
        }
        Ok(updates)
    }

    async fn wait_for_removal(&mut self) -> Result<u64> {
        let (id, responder) = self
            .watcher
            .try_next()
            .await?
            .and_then(|r| r.into_session_removed())
            .expect("Unwrapping watcher request");
        responder.send()?;
        Ok(id)
    }
}

struct TestPlayer {
    requests: PlayerRequestStream,
    id: u64,
}

impl TestPlayer {
    async fn new(service: &TestService) -> Result<Self> {
        let (player_client, player_server) = create_endpoints()?;
        let id = service
            .publisher
            .publish(player_client, PlayerRegistration { domain: Some(test_domain()) })
            .await?;
        let requests = player_server.into_stream()?;
        Ok(Self { requests, id })
    }

    async fn emit_delta(&mut self, delta: PlayerInfoDelta) -> Result<()> {
        match self.requests.try_next().await? {
            Some(PlayerRequest::WatchInfoChange { responder }) => responder.send(delta)?,
            _ => {
                panic!("Expected status change request.");
            }
        }

        Ok(())
    }

    async fn wait_for_request(&mut self, predicate: impl Fn(PlayerRequest) -> bool) -> Result<()> {
        while let Some(request) = self.requests.try_next().await? {
            if predicate(request) {
                return Ok(());
            }
        }
        panic!("Did not receive request that matched predicate. ")
    }
}

fn test_domain() -> String {
    String::from("domain://TEST")
}

fn delta_with_state(state: PlayerState) -> PlayerInfoDelta {
    PlayerInfoDelta {
        player_status: Some(PlayerStatus {
            player_state: Some(state),
            repeat_mode: Some(RepeatMode::Off),
            shuffle_on: Some(false),
            content_type: Some(ContentType::Audio),
            ..Decodable::new_empty()
        }),
        ..Decodable::new_empty()
    }
}

#[fasync::run_singlethreaded]
#[test]
async fn can_publish_players() -> Result<()> {
    let service = TestService::new()?;

    let mut player = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;

    player.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let mut sessions = watcher.wait_for_n_updates(1).await?;

    let (_id, delta) = sessions.remove(0);
    assert_eq!(delta.domain, Some(test_domain()));

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn can_receive_deltas() -> Result<()> {
    let service = TestService::new()?;

    let mut player1 = TestPlayer::new(&service).await?;
    let mut player2 = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;

    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    player2.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let _ = watcher.wait_for_n_updates(2).await?;

    player2
        .emit_delta(PlayerInfoDelta {
            player_capabilities: Some(PlayerCapabilities {
                flags: Some(PlayerCapabilityFlags::Play),
            }),
            ..Decodable::new_empty()
        })
        .await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    let (_id, delta) = updates.remove(0);
    assert_eq!(
        delta.player_capabilities,
        Some(PlayerCapabilities { flags: Some(PlayerCapabilityFlags::Play) })
    );

    player1
        .emit_delta(PlayerInfoDelta {
            player_capabilities: Some(PlayerCapabilities {
                flags: Some(PlayerCapabilityFlags::Pause),
            }),
            ..Decodable::new_empty()
        })
        .await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    let (_id, delta) = updates.remove(0);
    assert_eq!(
        delta.player_capabilities,
        Some(PlayerCapabilities { flags: Some(PlayerCapabilityFlags::Pause) })
    );

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn active_status() -> Result<()> {
    let service = TestService::new()?;

    let mut player1 = TestPlayer::new(&service).await?;
    let mut player2 = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;

    player1.emit_delta(delta_with_state(PlayerState::Idle)).await?;
    player2.emit_delta(delta_with_state(PlayerState::Idle)).await?;
    let _ = watcher.wait_for_n_updates(2).await?;

    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    let (active_id, delta) = updates.remove(0);
    assert_eq!(delta.is_locally_active, Some(true));

    player2.emit_delta(delta_with_state(PlayerState::Paused)).await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    let (new_active_id, delta) = updates.remove(0);
    assert_eq!(delta.is_locally_active, Some(false));

    assert_ne!(new_active_id, active_id);

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn player_controls_are_proxied() -> Result<()> {
    let service = TestService::new()?;

    let mut player = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;

    player.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    let (id, _) = updates.remove(0);

    // We take the watch request from the player's queue and don't answer it, so that
    // the stream of requests coming in that we match on down below doesn't contain it.
    let _watch_request = player.requests.try_next().await?;

    let (session_client, session_server) = create_endpoints()?;
    let session: SessionControlProxy = session_client.into_proxy()?;
    session.play()?;
    service.discovery.connect_to_session(id, session_server)?;

    player
        .wait_for_request(|request| match request {
            PlayerRequest::Play { .. } => true,
            _ => false,
        })
        .await?;

    let (_volume_client, volume_server) = create_endpoints()?;
    session.bind_volume_control(volume_server)?;
    player
        .wait_for_request(|request| match request {
            PlayerRequest::BindVolumeControl { .. } => true,
            _ => false,
        })
        .await
}

#[fasync::run_singlethreaded]
#[test]
async fn player_disconnection_propagates() -> Result<()> {
    let service = TestService::new()?;

    let mut player = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;

    player.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    let (id, _) = updates.remove(0);

    let (session_client, session_server) = create_endpoints()?;
    let session: SessionControlProxy = session_client.into_proxy()?;
    service.discovery.connect_to_session(id, session_server)?;

    drop(player);
    watcher.wait_for_removal().await?;
    let session_channel =
        session.into_channel().expect("Taking channel from proxy").into_zx_channel();
    session_channel.wait_handle(zx::Signals::CHANNEL_PEER_CLOSED, zx::Time::INFINITE)?;

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn watch_filter_active() -> Result<()> {
    let service = TestService::new()?;

    let mut player1 = TestPlayer::new(&service).await?;
    let mut player2 = TestPlayer::new(&service).await?;
    let _player3 = TestPlayer::new(&service).await?;
    let mut active_watcher =
        service.new_watcher(WatchOptions { only_active: Some(true), ..Decodable::new_empty() })?;

    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let updates = active_watcher.wait_for_n_updates(1).await?;
    assert_eq!(updates.len(), 1);
    assert_eq!(updates[0].1.is_locally_active, Some(true), "Update: {:?}", updates[0]);
    let player1_id = updates[0].0;

    player2.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let updates = active_watcher.wait_for_n_updates(1).await?;
    assert_eq!(updates.len(), 1);
    assert_eq!(updates[0].1.is_locally_active, Some(true), "Update: {:?}", updates[1]);

    player1.emit_delta(delta_with_state(PlayerState::Paused)).await?;
    assert_eq!(active_watcher.wait_for_removal().await?, player1_id);

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn disconnected_player_results_in_removal_event() -> Result<()> {
    let service = TestService::new()?;

    let mut player1 = TestPlayer::new(&service).await?;
    let _player2 = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;

    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let _updates = watcher.wait_for_n_updates(2).await?;

    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    assert_eq!(updates.len(), 1);
    let (player1_id, _) = updates.remove(0);

    drop(player1);
    let removed_id = watcher.wait_for_removal().await?;
    assert_eq!(player1_id, removed_id);

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn players_get_ids() -> Result<()> {
    let service = TestService::new()?;

    let player1 = TestPlayer::new(&service).await?;
    let player2 = TestPlayer::new(&service).await?;

    assert_ne!(player1.id, player2.id);

    Ok(())
}

#[fasync::run_singlethreaded]
#[test]
async fn users_can_watch_session_status() -> Result<()> {
    let service = TestService::new()?;

    let mut player1 = TestPlayer::new(&service).await?;
    let mut player2 = TestPlayer::new(&service).await?;

    let (session1, session1_request) = create_proxy()?;
    service.discovery.connect_to_session(player1.id, session1_request)?;

    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let status1 = session1.watch_status().await?;
    assert_matches!(
        status1.player_status,
        Some(PlayerStatus { player_state: Some(PlayerState::Playing), .. })
    );

    player2.emit_delta(delta_with_state(PlayerState::Buffering)).await?;
    player1.emit_delta(delta_with_state(PlayerState::Paused)).await?;
    let _status2 = session1.watch_status().await?;

    Ok(())
}
