// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{Result, SessionId};
use anyhow::Context as _;
use assert_matches::assert_matches;
use diagnostics_reader::{ArchiveReader, ComponentSelector, Inspect};
use fidl::encoding::Decodable;
use fidl::endpoints::{create_endpoints, create_proxy, create_request_stream};
use fidl_fuchsia_diagnostics::*;
use fidl_fuchsia_logger::LogSinkMarker;
use fidl_fuchsia_media::*;
use fidl_fuchsia_media_sessions2::*;
use fuchsia_async as fasync;
use fuchsia_component::{client, server::*};
use fuchsia_component_test::{
    Capability, ChildOptions, LocalComponentHandles, RealmBuilder, RealmInstance, Ref, Route,
};
use fuchsia_inspect as inspect;
use futures::{
    self,
    channel::mpsc,
    sink::SinkExt,
    stream::{StreamExt, TryStreamExt},
};
use std::collections::HashMap;

const MEDIASESSION_URL: &str = "#meta/mediasession.cm";
const MEDIASESSION_NAME: &str = "mediasession";

struct TestService {
    #[allow(unused)]
    realm: RealmInstance,
    publisher: PublisherProxy,
    discovery: DiscoveryProxy,
    archive: ArchiveAccessorProxy,
    observer_discovery: ObserverDiscoveryProxy,
    new_usage_watchers: mpsc::Receiver<(AudioRenderUsage, UsageWatcherProxy)>,
    usage_watchers: HashMap<AudioRenderUsage, UsageWatcherProxy>,
}

impl TestService {
    async fn new() -> Result<Self> {
        let builder = RealmBuilder::new().await.unwrap();
        let mediasession = builder
            .add_child(MEDIASESSION_NAME, MEDIASESSION_URL, ChildOptions::new())
            .await
            .unwrap();
        let (new_usage_watchers_sink, new_usage_watchers) = mpsc::channel(10);
        let usage_reporter = builder
            .add_local_child(
                "usage_reporter",
                move |handles: LocalComponentHandles| {
                    let new_usage_watchers_sink = new_usage_watchers_sink.clone();
                    Box::pin(Self::usage_reporter_mock(handles, new_usage_watchers_sink))
                },
                ChildOptions::new(),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<LogSinkMarker>())
                    .from(Ref::parent())
                    .to(&mediasession),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<PublisherMarker>())
                    .from(&mediasession)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<DiscoveryMarker>())
                    .from(&mediasession)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<ObserverDiscoveryMarker>())
                    .from(&mediasession)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<ActiveSessionMarker>())
                    .from(&mediasession)
                    .to(Ref::parent()),
            )
            .await
            .unwrap();
        builder
            .add_route(
                Route::new()
                    .capability(Capability::protocol::<UsageReporterMarker>())
                    .from(&usage_reporter)
                    .to(&mediasession),
            )
            .await?;

        let realm = builder.build().await.unwrap();

        let publisher = realm
            .root
            .connect_to_protocol_at_exposed_dir::<PublisherMarker>()
            .context("Connecting to Publisher")?;
        let discovery = realm
            .root
            .connect_to_protocol_at_exposed_dir::<DiscoveryMarker>()
            .context("Connecting to Discovery")?;
        let observer_discovery = realm
            .root
            .connect_to_protocol_at_exposed_dir::<ObserverDiscoveryMarker>()
            .context("Connecting to ObserverDiscovery")?;
        let archive = client::connect_to_protocol::<ArchiveAccessorMarker>()
            .context("Connecting to ArchiveAccessor")?;

        Ok(Self {
            realm,
            publisher,
            discovery,
            archive,
            observer_discovery,
            new_usage_watchers,
            usage_watchers: HashMap::new(),
        })
    }

    async fn usage_reporter_mock(
        handles: LocalComponentHandles,
        new_usage_watchers_sink: mpsc::Sender<(AudioRenderUsage, UsageWatcherProxy)>,
    ) -> Result<()> {
        let mut fs = ServiceFs::new();
        let mut tasks = vec![];

        fs.dir("svc").add_fidl_service::<_, UsageReporterRequestStream>(
            move |mut request_stream| {
                let mut new_usage_watchers_sink = new_usage_watchers_sink.clone();
                tasks.push(fasync::Task::local(async move {
                    while let Some(Ok(UsageReporterRequest::Watch {
                        usage, usage_watcher, ..
                    })) = request_stream.next().await
                    {
                        match (usage, usage_watcher.into_proxy()) {
                            (Usage::RenderUsage(usage), Ok(usage_watcher)) => {
                                new_usage_watchers_sink
                                    .send((usage, usage_watcher))
                                    .await
                                    .expect("Forwarding new UsageWatcher from service under test");
                            }
                            (_, Ok(_)) => {
                                println!("Service under test tried to watch a capture usage")
                            }
                            (_, Err(e)) => println!("Service under test sent bad request: {:?}", e),
                        }
                    }
                }));
            },
        );

        fs.serve_connection(handles.outgoing_dir)?;
        fs.collect::<()>().await;

        Ok(())
    }

    fn new_watcher(&self, watch_options: WatchOptions) -> Result<TestWatcher> {
        let (watcher_client, watcher_server) =
            create_endpoints().context("Creating watcher endpoints")?;
        self.discovery.watch_sessions(watch_options, watcher_client)?;
        Ok(TestWatcher {
            watcher: watcher_server.into_stream().context("Turning watcher into stream")?,
        })
    }

    fn new_observer_watcher(&self, watch_options: WatchOptions) -> Result<TestWatcher> {
        let (watcher_client, watcher_server) =
            create_endpoints().context("Creating observer watcher endpoints")?;
        self.observer_discovery.watch_sessions(watch_options, watcher_client)?;
        Ok(TestWatcher {
            watcher: watcher_server
                .into_stream()
                .context("Turning observer watcher into stream")?,
        })
    }

    async fn dequeue_watcher(&mut self) {
        if let Some((usage, watcher)) = self.new_usage_watchers.next().await {
            self.usage_watchers.insert(usage, watcher);
        } else {
            panic!("Watcher channel closed.")
        }
    }

    async fn start_interruption(&mut self, usage: AudioRenderUsage) {
        if let Some(watcher) = self.usage_watchers.get(&usage) {
            watcher
                .on_state_changed(
                    &mut Usage::RenderUsage(usage),
                    &mut UsageState::Muted(UsageStateMuted::EMPTY),
                )
                .await
                .expect("Sending interruption start to service under test");
        } else {
            panic!("Can't start interruption; no watcher is registered for usage {:?}", usage)
        }
    }

    async fn stop_interruption(&mut self, usage: AudioRenderUsage) {
        if let Some(watcher) = self.usage_watchers.get(&usage) {
            watcher
                .on_state_changed(
                    &mut Usage::RenderUsage(usage),
                    &mut UsageState::Unadjusted(UsageStateUnadjusted::EMPTY),
                )
                .await
                .expect("Sending interruption stop to service under test");
        } else {
            panic!("Can't stop interruption; no watcher is registered for usage {:?}", usage)
        }
    }

    async fn inspect_tree(&mut self) -> inspect::hierarchy::DiagnosticsHierarchy {
        ArchiveReader::new()
            .with_archive(self.archive.clone())
            .add_selector(ComponentSelector::new(vec![format!(
                "realm_builder\\:{}/{}",
                self.realm.root.child_name(),
                MEDIASESSION_NAME,
            )]))
            .snapshot::<Inspect>()
            .await
            .expect("Got batch")
            .into_iter()
            .next()
            .and_then(|result| result.payload)
            .expect("Got payload")
    }
}

struct TestWatcher {
    watcher: SessionsWatcherRequestStream,
}

impl TestWatcher {
    async fn wait_for_n_updates(&mut self, n: usize) -> Result<Vec<(SessionId, SessionInfoDelta)>> {
        let mut updates: Vec<(SessionId, SessionInfoDelta)> = vec![];
        for i in 0..n {
            let (id, delta, responder) = self
                .watcher
                .try_next()
                .await?
                .and_then(|r| r.into_session_updated())
                .with_context(|| format!("Unwrapping watcher request {:?}", i))?;
            responder.send().with_context(|| format!("Sending ack for watcher request {:?}", i))?;
            updates.push((id, delta));
        }
        Ok(updates)
    }

    async fn wait_for_removal(&mut self) -> Result<SessionId> {
        let (id, responder) = self
            .watcher
            .try_next()
            .await?
            .and_then(|r| r.into_session_removed())
            .context("Unwrapping watcher request for awaited removal")?;
        responder.send().context("Sending ack for removal")?;
        Ok(id)
    }
}

struct TestPlayer {
    requests: PlayerRequestStream,
    id: SessionId,
}

impl TestPlayer {
    async fn new(service: &TestService) -> Result<Self> {
        let (player_client, requests) =
            create_request_stream().context("Creating player request stream")?;
        let id = service
            .publisher
            .publish(
                player_client,
                PlayerRegistration { domain: Some(test_domain()), ..Decodable::new_empty() },
            )
            .await
            .context("Registering new player")?;
        Ok(Self { requests, id })
    }

    async fn emit_delta(&mut self, delta: PlayerInfoDelta) -> Result<()> {
        match self.requests.try_next().await? {
            Some(PlayerRequest::WatchInfoChange { responder }) => responder.send(delta)?,
            _ => {
                return Err(anyhow::anyhow!("Expected status change request."));
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
        Err(anyhow::anyhow!("Did not receive request that matched predicate."))
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

fn local_delta_with_state(state: PlayerState) -> PlayerInfoDelta {
    let mut delta = delta_with_state(state);
    delta.local = Some(true);
    delta
}

fn remote_delta_with_state(state: PlayerState) -> PlayerInfoDelta {
    let mut delta = delta_with_state(state);
    delta.local = Some(false);
    delta
}

fn delta_with_interruption(
    state: PlayerState,
    interruption_behavior: InterruptionBehavior,
) -> PlayerInfoDelta {
    let mut delta = delta_with_state(state);
    delta.interruption_behavior = Some(interruption_behavior);
    delta
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn can_publish_players() -> Result<()> {
    let service = TestService::new().await?;

    let mut player = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;

    player.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let mut sessions = watcher.wait_for_n_updates(1).await?;

    let (_id, delta) = sessions.remove(0);
    assert_eq!(delta.domain, Some(test_domain()));

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn can_receive_deltas() -> Result<()> {
    let service = TestService::new().await?;

    let mut player1 = TestPlayer::new(&service).await?;
    let mut player2 = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;

    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    player2.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let _ = watcher.wait_for_n_updates(2).await?;

    player2
        .emit_delta(PlayerInfoDelta {
            player_capabilities: Some(PlayerCapabilities {
                flags: Some(PlayerCapabilityFlags::PLAY),
                ..PlayerCapabilities::EMPTY
            }),
            ..Decodable::new_empty()
        })
        .await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    let (_id, delta) = updates.remove(0);
    assert_eq!(
        delta.player_capabilities,
        Some(PlayerCapabilities {
            flags: Some(PlayerCapabilityFlags::PLAY),
            ..PlayerCapabilities::EMPTY
        })
    );

    player1
        .emit_delta(PlayerInfoDelta {
            player_capabilities: Some(PlayerCapabilities {
                flags: Some(PlayerCapabilityFlags::PAUSE),
                ..PlayerCapabilities::EMPTY
            }),
            ..Decodable::new_empty()
        })
        .await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    let (_id, delta) = updates.remove(0);
    assert_eq!(
        delta.player_capabilities,
        Some(PlayerCapabilities {
            flags: Some(PlayerCapabilityFlags::PAUSE),
            ..PlayerCapabilities::EMPTY
        })
    );

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn active_status() -> Result<()> {
    let service = TestService::new().await?;

    let mut player = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;

    player.emit_delta(delta_with_state(PlayerState::Idle)).await?;
    let _ = watcher.wait_for_n_updates(1).await?;

    player.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    let (_, delta) = updates.remove(0);
    assert_eq!(
        delta.is_locally_active,
        Some(true),
        "Expected unknown locality playing state to be locally active."
    );

    player.emit_delta(delta_with_state(PlayerState::Paused)).await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    let (_, delta) = updates.remove(0);
    assert_eq!(
        delta.is_locally_active,
        Some(false),
        "Expected unknown locality paused state not to be locally active."
    );

    player.emit_delta(local_delta_with_state(PlayerState::Playing)).await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    let (_, delta) = updates.remove(0);
    assert_eq!(
        delta.is_locally_active,
        Some(true),
        "Expected local playing state to be locally active."
    );

    player.emit_delta(remote_delta_with_state(PlayerState::Playing)).await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    let (_, delta) = updates.remove(0);
    assert_eq!(
        delta.is_locally_active,
        Some(false),
        "Expected remote playing state not to be locally active."
    );

    player.emit_delta(local_delta_with_state(PlayerState::Playing)).await?;
    let mut updates = watcher.wait_for_n_updates(1).await?;
    let (_, delta) = updates.remove(0);
    assert_eq!(
        delta.is_locally_active,
        Some(true),
        "Expected local playing state to be locally active."
    );

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn player_controls_are_proxied() -> Result<()> {
    let service = TestService::new().await?;

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

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn player_disconnection_propagates() -> Result<()> {
    let service = TestService::new().await?;

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
    let mut session_events = session.take_event_stream();
    while let Some(_) = session_events.next().await {}

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn watch_filter_active() -> Result<()> {
    let service = TestService::new().await?;

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

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn disconnected_player_results_in_removal_event() -> Result<()> {
    let service = TestService::new().await?;

    let mut player1 = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;

    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let _updates = watcher.wait_for_n_updates(1).await?;

    let expected_id = player1.id;
    drop(player1);
    let removed_id = watcher.wait_for_removal().await?;
    assert_eq!(removed_id, expected_id);

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn player_status() -> Result<()> {
    let service = TestService::new().await?;

    let mut player = TestPlayer::new(&service).await?;

    let expected_player_status = || PlayerStatus {
        duration: Some(11),
        is_live: Some(true),
        player_state: Some(PlayerState::Playing),
        timeline_function: Some(TimelineFunction {
            subject_time: 0,
            reference_time: 10,
            subject_delta: 1,
            reference_delta: 1,
        }),
        repeat_mode: Some(RepeatMode::Group),
        shuffle_on: Some(true),
        content_type: Some(ContentType::Movie),
        error: Some(Error::Other),
        ..Decodable::new_empty()
    };

    player
        .emit_delta(PlayerInfoDelta {
            player_status: Some(expected_player_status()),
            ..Decodable::new_empty()
        })
        .await?;

    let (session, session_request) = create_proxy()?;
    service.discovery.connect_to_session(player.id, session_request)?;
    let status = session.watch_status().await.expect("Watching player status");
    let actual_player_status = status.player_status.expect("Unwrapping player status");

    assert_eq!(actual_player_status, expected_player_status());

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn player_capabilities() -> Result<()> {
    let service = TestService::new().await?;

    let mut player = TestPlayer::new(&service).await?;

    let expected_player_capabilities = || PlayerCapabilities {
        flags: Some(PlayerCapabilityFlags::PAUSE | PlayerCapabilityFlags::SKIP_FORWARD),
        ..PlayerCapabilities::EMPTY
    };

    player
        .emit_delta(PlayerInfoDelta {
            player_capabilities: Some(expected_player_capabilities()),
            ..Decodable::new_empty()
        })
        .await?;

    let (session, session_request) = create_proxy()?;
    service.discovery.connect_to_session(player.id, session_request)?;
    let status = session.watch_status().await.expect("Watching player capabilities");
    let actual_player_capabilities =
        status.player_capabilities.expect("Unwrapping player capabilities");

    assert_eq!(actual_player_capabilities, expected_player_capabilities());

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn media_images() -> Result<()> {
    let service = TestService::new().await?;

    let mut player = TestPlayer::new(&service).await?;

    let expected_media_images = || {
        vec![
            MediaImage {
                image_type: Some(MediaImageType::SourceIcon),
                sizes: Some(vec![ImageSizeVariant {
                    url: String::from("http://url1"),
                    width: 10,
                    height: 10,
                }]),
                ..MediaImage::EMPTY
            },
            MediaImage {
                image_type: Some(MediaImageType::Artwork),
                sizes: Some(vec![ImageSizeVariant {
                    url: String::from("http://url1"),
                    width: 10,
                    height: 10,
                }]),
                ..MediaImage::EMPTY
            },
        ]
    };

    player
        .emit_delta(PlayerInfoDelta {
            media_images: Some(expected_media_images()),
            ..Decodable::new_empty()
        })
        .await?;

    let (session, session_request) = create_proxy()?;
    service.discovery.connect_to_session(player.id, session_request)?;
    let status = session.watch_status().await.expect("Watching media images");
    let actual_media_images = status.media_images.expect("Unwrapping media images");

    assert_eq!(actual_media_images, expected_media_images());

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn players_get_ids() -> Result<()> {
    let service = TestService::new().await?;

    let player1 = TestPlayer::new(&service).await?;
    let player2 = TestPlayer::new(&service).await?;

    assert_ne!(player1.id, player2.id);

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn session_controllers_can_watch_session_status() -> Result<()> {
    let service = TestService::new().await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;

    let mut player1 = TestPlayer::new(&service).await?;
    let mut player2 = TestPlayer::new(&service).await?;

    let (session1, session1_request) = create_proxy()?;
    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let _updates = watcher.wait_for_n_updates(1).await?;

    service.discovery.connect_to_session(player1.id, session1_request)?;
    let status1 = session1.watch_status().await.context("Watching session status (1st time)")?;
    assert_matches!(
        status1.player_status,
        Some(PlayerStatus { player_state: Some(PlayerState::Playing), .. })
    );

    player2.emit_delta(delta_with_state(PlayerState::Buffering)).await?;
    player1.emit_delta(delta_with_state(PlayerState::Paused)).await?;
    let _updates = watcher.wait_for_n_updates(2).await?;
    let status1 = session1.watch_status().await.context("Watching session status (2nd time)")?;
    assert_matches!(
        status1.player_status,
        Some(PlayerStatus { player_state: Some(PlayerState::Paused), .. })
    );

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn session_observers_can_watch_session_status() -> Result<()> {
    let service = TestService::new().await?;
    let mut watcher = service.new_observer_watcher(Decodable::new_empty())?;

    let mut player1 = TestPlayer::new(&service).await?;
    let mut player2 = TestPlayer::new(&service).await?;

    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let _updates = watcher.wait_for_n_updates(1).await?;

    let (session1, session1_request) = create_proxy()?;
    service.observer_discovery.connect_to_session(player1.id, session1_request)?;
    let status1 = session1.watch_status().await.context("Watching session status (1st time)")?;
    assert_matches!(
        status1.player_status,
        Some(PlayerStatus { player_state: Some(PlayerState::Playing), .. })
    );

    player2.emit_delta(delta_with_state(PlayerState::Buffering)).await?;
    player1.emit_delta(delta_with_state(PlayerState::Paused)).await?;
    let _updates = watcher.wait_for_n_updates(2).await?;
    let status1 = session1.watch_status().await.context("Watching session status (2nd time)")?;
    assert_matches!(
        status1.player_status,
        Some(PlayerStatus { player_state: Some(PlayerState::Paused), .. })
    );

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn player_disconnection_disconects_observers() -> Result<()> {
    let service = TestService::new().await?;
    let mut watcher = service.new_observer_watcher(Decodable::new_empty())?;

    let mut player = TestPlayer::new(&service).await?;

    player.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let _updates = watcher.wait_for_n_updates(1).await?;

    let (session, session_request) = create_proxy()?;
    service.observer_discovery.connect_to_session(player.id, session_request)?;
    assert!(session.watch_status().await.is_ok());

    drop(player);
    while let Ok(_) = session.watch_status().await {}

    // Passes by terminating, indicating the observer is disconnected.

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn observers_caught_up_with_state_of_session() -> Result<()> {
    let service = TestService::new().await?;
    let mut watcher = service.new_observer_watcher(Decodable::new_empty())?;

    let mut player = TestPlayer::new(&service).await?;

    player.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let _updates = watcher.wait_for_n_updates(1).await?;

    let (session1, session1_request) = create_proxy()?;
    service.observer_discovery.connect_to_session(player.id, session1_request)?;
    let status1 = session1.watch_status().await.context("Watching session status (1st time)")?;
    assert_matches!(
        status1.player_status,
        Some(PlayerStatus { player_state: Some(PlayerState::Playing), .. })
    );

    let (session2, session2_request) = create_proxy()?;
    service.observer_discovery.connect_to_session(player.id, session2_request)?;
    let status2 = session2.watch_status().await.context("Watching session status (2nd time)")?;
    assert_matches!(
        status2.player_status,
        Some(PlayerStatus { player_state: Some(PlayerState::Playing), .. })
    );

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn player_is_interrupted() -> Result<()> {
    let mut service = TestService::new().await?;
    let mut player = TestPlayer::new(&service).await?;

    player
        .emit_delta(delta_with_interruption(PlayerState::Playing, InterruptionBehavior::Pause))
        .await?;
    service.dequeue_watcher().await;

    // We take the watch request from the player's queue and don't answer it, so that
    // the stream of requests coming in that we match on down below doesn't contain it.
    let _watch_request = player.requests.try_next().await?;

    service.start_interruption(AudioRenderUsage::Media).await;
    player
        .wait_for_request(|request| matches!(request, PlayerRequest::Pause { .. }))
        .await
        .expect("Waiting for player to receive pause");

    service.stop_interruption(AudioRenderUsage::Media).await;
    player
        .wait_for_request(|request| matches!(request, PlayerRequest::Play { .. }))
        .await
        .expect("Waiting for player to receive `Play` command");

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn unenrolled_player_is_not_paused_when_interrupted() -> Result<()> {
    let mut service = TestService::new().await?;
    let mut player1 = TestPlayer::new(&service).await?;
    let mut player2 = TestPlayer::new(&service).await?;

    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    player2
        .emit_delta(delta_with_interruption(PlayerState::Playing, InterruptionBehavior::Pause))
        .await?;
    service.dequeue_watcher().await;

    // We take the watch request from the player's queue and don't answer it, so that
    // the stream of requests coming in that we match on down below doesn't contain it.
    let _watch_request1 = player1.requests.try_next().await?;
    let _watch_request2 = player2.requests.try_next().await?;

    service.start_interruption(AudioRenderUsage::Media).await;
    player2
        .wait_for_request(|request| matches!(request, PlayerRequest::Pause { .. }))
        .await
        .expect("Waiting for player to receive pause");

    drop(service);
    let next = player1.requests.try_next().await?;
    assert!(next.is_none());

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn player_paused_before_interruption_is_not_resumed_by_its_end() -> Result<()> {
    let mut service = TestService::new().await?;
    let mut player1 = TestPlayer::new(&service).await?;
    let mut player2 = TestPlayer::new(&service).await?;

    player1
        .emit_delta(delta_with_interruption(PlayerState::Playing, InterruptionBehavior::Pause))
        .await?;
    player2
        .emit_delta(delta_with_interruption(PlayerState::Paused, InterruptionBehavior::Pause))
        .await?;
    service.dequeue_watcher().await;

    // We take the watch request from the player's queue and don't answer it, so that
    // the stream of requests coming in that we match on down below doesn't contain it.
    let _watch_request1 = player1.requests.try_next().await?;
    let _watch_request2 = player2.requests.try_next().await?;

    service.start_interruption(AudioRenderUsage::Media).await;
    player1
        .wait_for_request(|request| matches!(request, PlayerRequest::Pause { .. }))
        .await
        .expect("Waiting for player to receive pause");

    service.stop_interruption(AudioRenderUsage::Media).await;
    player1
        .wait_for_request(|request| matches!(request, PlayerRequest::Play { .. }))
        .await
        .expect("Waiting for player to receive play");

    drop(service);
    let next = player2.requests.try_next().await?;
    assert!(next.is_none());

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn player_paused_during_interruption_is_not_resumed_by_its_end() -> Result<()> {
    let mut service = TestService::new().await?;
    let mut player = TestPlayer::new(&service).await?;
    let (session, session_server) = create_proxy()?;
    service.discovery.connect_to_session(player.id, session_server)?;

    player
        .emit_delta(delta_with_interruption(PlayerState::Playing, InterruptionBehavior::Pause))
        .await?;
    service.dequeue_watcher().await;

    // We take the watch request from the player's queue and don't answer it, so that
    // the stream of requests coming in that we match on down below doesn't contain it.
    let _watch_request = player.requests.try_next().await?;

    service.start_interruption(AudioRenderUsage::Media).await;
    player
        .wait_for_request(|request| matches!(request, PlayerRequest::Pause { .. }))
        .await
        .expect("Waiting for player to receive pause");

    session.pause()?;
    player
        .wait_for_request(|request| matches!(request, PlayerRequest::Pause { .. }))
        .await
        .expect("Waiting for player to receive pause");

    service.stop_interruption(AudioRenderUsage::Media).await;

    drop(service);
    let next = player.requests.try_next().await?;
    assert!(next.is_none());

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn active_session_initializes_clients_without_player() -> Result<()> {
    let service = TestService::new().await?;
    let active_session_discovery = service
        .realm
        .root
        .connect_to_protocol_at_exposed_dir::<ActiveSessionMarker>()
        .context("Connecting to Active Session service")?;

    let session = active_session_discovery
        .watch_active_session()
        .await
        .context("Watching the active session")?;
    assert_matches!(session, None);

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn active_session_initializes_clients_with_idle_player() -> Result<()> {
    let service = TestService::new().await?;
    let mut player = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;
    let active_session_discovery = service
        .realm
        .root
        .connect_to_protocol_at_exposed_dir::<ActiveSessionMarker>()
        .context("Connecting to Active Session service")?;

    player.emit_delta(delta_with_state(PlayerState::Idle)).await?;
    let _ = watcher.wait_for_n_updates(1).await?;

    let session = active_session_discovery
        .watch_active_session()
        .await
        .context("Watching the active session")?;
    assert_matches!(session, None);

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn active_session_initializes_clients_with_active_player() -> Result<()> {
    let service = TestService::new().await?;
    let mut player = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;
    let active_session_discovery = service
        .realm
        .root
        .connect_to_protocol_at_exposed_dir::<ActiveSessionMarker>()
        .context("Connecting to Active Session service")?;

    player.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let _ = watcher.wait_for_n_updates(1).await?;

    // We take the watch request from the player's queue and don't answer it, so that
    // the stream of requests coming in that we match on down below doesn't contain it.
    let _watch_request = player.requests.try_next().await?;

    let session = active_session_discovery
        .watch_active_session()
        .await
        .context("Watching the active session")?;
    let session = session.expect("Unwrapping active session channel");
    let session = session.into_proxy().expect("Creating session proxy");
    session.play().context("Sending play command to session")?;

    player
        .wait_for_request(|request| matches!(request, PlayerRequest::Play { .. }))
        .await
        .expect("Waiting for player to receive play command");

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn active_session_falls_back_when_session_removed() -> Result<()> {
    let service = TestService::new().await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;
    let active_session_discovery = service
        .realm
        .root
        .connect_to_protocol_at_exposed_dir::<ActiveSessionMarker>()
        .context("Connecting to Active Session service")?;

    let mut player1 = TestPlayer::new(&service).await?;
    let mut player2 = TestPlayer::new(&service).await?;

    let session =
        active_session_discovery.watch_active_session().await.context("Syncing active session")?;
    assert!(session.is_none());

    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let _ = watcher.wait_for_n_updates(1).await?;

    player2.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let _ = watcher.wait_for_n_updates(1).await?;

    player1.emit_delta(delta_with_state(PlayerState::Paused)).await?;
    let _ = watcher.wait_for_n_updates(1).await?;

    let session = active_session_discovery
        .watch_active_session()
        .await
        .context("Watching active session 1st time")?;
    let _session = session.expect("Unwrapping active session channel");

    drop(player2);
    let _ = watcher.wait_for_removal().await?;

    let session = active_session_discovery
        .watch_active_session()
        .await
        .context("Watching the active session 2nd time")?;
    let session = session.expect("Unwrapping active session channel 2nd time");
    let session = session.into_proxy().expect("Creating session proxy 2nd time");

    let info_delta = session.watch_status().await.expect("Watching session status");
    assert_eq!(
        info_delta.player_status.and_then(|status| status.player_state),
        Some(PlayerState::Paused)
    );

    drop(player1);
    let _ = watcher.wait_for_removal().await?;
    assert!(session.watch_status().await.is_err());

    Ok(())
}

#[fuchsia::test(logging_tags = ["mediasession_tests"])]
async fn inspect_tree_correct() -> Result<()> {
    let mut service = TestService::new().await?;
    let player1 = TestPlayer::new(&service).await?;
    let player2 = TestPlayer::new(&service).await?;
    let ids = vec![format!("{}", player1.id), format!("{}", player2.id)];

    let hierarchy = service.inspect_tree().await;
    assert_eq!(hierarchy.children.len(), 1);
    let players = &hierarchy.children[0];
    assert_eq!(players.name, "players");
    assert_eq!(players.children.len(), 2);
    assert!(ids.contains(&players.children[0].name));
    assert!(ids.contains(&players.children[1].name));

    Ok(())
}
