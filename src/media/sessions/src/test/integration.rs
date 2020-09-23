// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{Result, SessionId};
use anyhow::Context as _;
use fdio;
use fidl::encoding::Decodable;
use fidl::endpoints::DiscoverableService;
use fidl::endpoints::{create_endpoints, create_proxy, create_request_stream};
use fidl_fuchsia_inspect::*;
use fidl_fuchsia_logger::LogSinkMarker;
use fidl_fuchsia_media::*;
use fidl_fuchsia_media_sessions2::*;
use fidl_fuchsia_sys::ComponentControllerEvent;
use fuchsia_async as fasync;
use fuchsia_component as comp;
use fuchsia_component::server::*;
use fuchsia_inspect as inspect;
use futures::{
    self,
    channel::mpsc,
    future,
    sink::SinkExt,
    stream::{StreamExt, TryStreamExt},
};
use glob::glob;
use lazy_static::lazy_static;
use matches::assert_matches;
use std::collections::HashMap;

const MEDIASESSION_URL: &str = "fuchsia-pkg://fuchsia.com/mediasession#meta/mediasession.cmx";
const MEDIASESSION_CMX: &str = "mediasession.cmx";

lazy_static! {
    static ref LOGGER: () = {
        fuchsia_syslog::init_with_tags(&["mediasession_tests"]).expect("Initializing syslogger");
    };
}

struct TestService {
    // This needs to stay alive to keep the service running.
    app: comp::client::App,
    // This needs to stay alive to keep the service running.
    #[allow(unused)]
    env: NestedEnvironment,
    publisher: PublisherProxy,
    discovery: DiscoveryProxy,
    observer_discovery: ObserverDiscoveryProxy,
    new_usage_watchers: mpsc::Receiver<(AudioRenderUsage, UsageWatcherProxy)>,
    usage_watchers: HashMap<AudioRenderUsage, UsageWatcherProxy>,
}

impl TestService {
    fn new() -> Result<Self> {
        let mut fs = ServiceFs::new();
        fs.add_proxy_service::<LogSinkMarker, ()>();

        let (new_usage_watchers_sink, new_usage_watchers) = mpsc::channel(10);
        fs.add_fidl_service::<_, UsageReporterRequestStream>(move |mut request_stream| {
            let mut new_usage_watchers_sink = new_usage_watchers_sink.clone();
            fasync::Task::spawn(async move {
                while let Some(Ok(UsageReporterRequest::Watch { usage, usage_watcher, .. })) =
                    request_stream.next().await
                {
                    match (usage, usage_watcher.into_proxy()) {
                        (Usage::RenderUsage(usage), Ok(usage_watcher)) => {
                            new_usage_watchers_sink
                                .send((usage, usage_watcher))
                                .await
                                .expect("Forwarding new UsageWatcher from service under test");
                        }
                        (_, Ok(_)) => println!("Service under test tried to watch a capture usage"),
                        (_, Err(e)) => println!("Service under test sent bad request: {:?}", e),
                    }
                }
            })
            .detach()
        });

        let env = fs.create_salted_nested_environment("environment")?;

        fasync::Task::spawn(fs.for_each(|_| future::ready(()))).detach();

        let mediasession = comp::client::launch(
            env.launcher(),
            String::from(MEDIASESSION_URL),
            /*arguments=*/ None,
        )
        .context("Launching mediasession")?;

        let publisher = mediasession
            .connect_to_service::<PublisherMarker>()
            .context("Connecting to Publisher")?;
        let discovery = mediasession
            .connect_to_service::<DiscoveryMarker>()
            .context("Connecting to Discovery")?;
        let observer_discovery = mediasession
            .connect_to_service::<ObserverDiscoveryMarker>()
            .context("Connecting to ObserverDiscovery")?;

        Ok(Self {
            app: mediasession,
            env,
            publisher,
            discovery,
            observer_discovery,
            new_usage_watchers,
            usage_watchers: HashMap::new(),
        })
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
                    &mut UsageState::Muted(UsageStateMuted::empty()),
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
                    &mut UsageState::Unadjusted(UsageStateUnadjusted::empty()),
                )
                .await
                .expect("Sending interruption stop to service under test");
        } else {
            panic!("Can't stop interruption; no watcher is registered for usage {:?}", usage)
        }
    }

    async fn read_inspect(&mut self) -> inspect::NodeHierarchy {
        let mut component_stream = self.app.controller().take_event_stream();
        match component_stream
            .next()
            .await
            .expect("component event stream ended before termination event")
            .expect("event stream returned error")
        {
            ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
                panic!(
                    "Component terminated unexpectedly. Code: {}. Reason: {:?}",
                    return_code, termination_reason
                );
            }
            ComponentControllerEvent::OnDirectoryReady {} => {
                let pattern = format!(
                    "/hub/r/*/*/c/{}/*/out/diagnostics/{}",
                    MEDIASESSION_CMX,
                    TreeMarker::SERVICE_NAME
                );
                let path = glob(&pattern)
                    .expect("glob")
                    .next()
                    .expect("failed to get pattern match")
                    .expect("failed to parse glob");
                let (tree, server_end) =
                    fidl::endpoints::create_proxy::<TreeMarker>().expect("failed to create proxy");
                fdio::service_connect(
                    &path.to_string_lossy().to_string(),
                    server_end.into_channel(),
                )
                .expect("failed to connect to service");

                inspect::reader::read_from_tree(&tree)
                    .await
                    .expect("failed to get inspect hierarchy")
            }
        }
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

macro_rules! test {
    ($name:ident, $test:expr) => {
        #[fasync::run_singlethreaded(test)]
        async fn $name() -> Result<()> {
            *LOGGER;
            $test().await
        }
    };
}

test!(can_publish_players, || async {
    let service = TestService::new()?;

    let mut player = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;

    player.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let mut sessions = watcher.wait_for_n_updates(1).await?;

    let (_id, delta) = sessions.remove(0);
    assert_eq!(delta.domain, Some(test_domain()));

    Ok(())
});

test!(can_receive_deltas, || async {
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
});

test!(active_status, || async {
    let service = TestService::new()?;

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
});

test!(player_controls_are_proxied, || async {
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
});

test!(player_disconnection_propagates, || async {
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
    let mut session_events = session.take_event_stream();
    while let Some(_) = session_events.next().await {}

    Ok(())
});

test!(watch_filter_active, || async {
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
});

test!(disconnected_player_results_in_removal_event, || async {
    let service = TestService::new()?;

    let mut player1 = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;

    player1.emit_delta(delta_with_state(PlayerState::Playing)).await?;
    let _updates = watcher.wait_for_n_updates(1).await?;

    let expected_id = player1.id;
    drop(player1);
    let removed_id = watcher.wait_for_removal().await?;
    assert_eq!(removed_id, expected_id);

    Ok(())
});

test!(player_status, || async {
    let service = TestService::new()?;

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
});

test!(player_capabilities, || async {
    let service = TestService::new()?;

    let mut player = TestPlayer::new(&service).await?;

    let expected_player_capabilities = || PlayerCapabilities {
        flags: Some(PlayerCapabilityFlags::Pause | PlayerCapabilityFlags::SkipForward),
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
});

test!(media_images, || async {
    let service = TestService::new()?;

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
            },
            MediaImage {
                image_type: Some(MediaImageType::Artwork),
                sizes: Some(vec![ImageSizeVariant {
                    url: String::from("http://url1"),
                    width: 10,
                    height: 10,
                }]),
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
});

test!(players_get_ids, || async {
    let service = TestService::new()?;

    let player1 = TestPlayer::new(&service).await?;
    let player2 = TestPlayer::new(&service).await?;

    assert_ne!(player1.id, player2.id);

    Ok(())
});

test!(session_controllers_can_watch_session_status, || async {
    let service = TestService::new()?;
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
});

test!(session_observers_can_watch_session_status, || async {
    let service = TestService::new()?;
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
});

test!(player_disconnection_disconects_observers, || async {
    let service = TestService::new()?;
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
});

test!(observers_caught_up_with_state_of_session, || async {
    let service = TestService::new()?;
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
});

test!(player_is_interrupted, || async {
    let mut service = TestService::new()?;
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
        .wait_for_request(|request| matches!(request, PlayerRequest::Pause {..}))
        .await
        .expect("Waiting for player to receive pause");

    service.stop_interruption(AudioRenderUsage::Media).await;
    player
        .wait_for_request(|request| matches!(request, PlayerRequest::Play {..}))
        .await
        .expect("Waiting for player to receive `Play` command");

    Ok(())
});

test!(unenrolled_player_is_not_paused_when_interrupted, || async {
    let mut service = TestService::new()?;
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
        .wait_for_request(|request| matches!(request, PlayerRequest::Pause {..}))
        .await
        .expect("Waiting for player to receive pause");

    drop(service);
    let next = player1.requests.try_next().await?;
    assert!(next.is_none());

    Ok(())
});

test!(player_paused_before_interruption_is_not_resumed_by_its_end, || async {
    let mut service = TestService::new()?;
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
        .wait_for_request(|request| matches!(request, PlayerRequest::Pause {..}))
        .await
        .expect("Waiting for player to receive pause");

    service.stop_interruption(AudioRenderUsage::Media).await;
    player1
        .wait_for_request(|request| matches!(request, PlayerRequest::Play {..}))
        .await
        .expect("Waiting for player to receive play");

    drop(service);
    let next = player2.requests.try_next().await?;
    assert!(next.is_none());

    Ok(())
});

// TODO(52668) Re-enable when publish/discover race is fixed
#[ignore]
#[fasync::run_singlethreaded(test)]
async fn player_paused_during_interruption_is_not_resumed_by_its_end() -> Result<()> {
    *LOGGER;
    let mut service = TestService::new()?;
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
        .wait_for_request(|request| matches!(request, PlayerRequest::Pause {..}))
        .await
        .expect("Waiting for player to receive pause");

    session.pause()?;
    player
        .wait_for_request(|request| matches!(request, PlayerRequest::Pause {..}))
        .await
        .expect("Waiting for player to receive pause");

    service.stop_interruption(AudioRenderUsage::Media).await;

    drop(service);
    let next = player.requests.try_next().await?;
    assert!(next.is_none());

    Ok(())
}

test!(active_session_initializes_clients_without_player, || async {
    let service = TestService::new()?;
    let active_session_discovery = service
        .app
        .connect_to_service::<ActiveSessionMarker>()
        .context("Connecting to Active Session service")?;

    let session = active_session_discovery
        .watch_active_session()
        .await
        .context("Watching the active session")?;
    assert_matches!(session, None);

    Ok(())
});

test!(active_session_initializes_clients_with_idle_player, || async {
    let service = TestService::new()?;
    let mut player = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;
    let active_session_discovery = service
        .app
        .connect_to_service::<ActiveSessionMarker>()
        .context("Connecting to Active Session service")?;

    player.emit_delta(delta_with_state(PlayerState::Idle)).await?;
    let _ = watcher.wait_for_n_updates(1).await?;

    let session = active_session_discovery
        .watch_active_session()
        .await
        .context("Watching the active session")?;
    assert_matches!(session, None);

    Ok(())
});

test!(active_session_initializes_clients_with_active_player, || async {
    let service = TestService::new()?;
    let mut player = TestPlayer::new(&service).await?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;
    let active_session_discovery = service
        .app
        .connect_to_service::<ActiveSessionMarker>()
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
        .wait_for_request(|request| matches!(request, PlayerRequest::Play {..}))
        .await
        .expect("Waiting for player to receive play command");

    Ok(())
});

test!(active_session_falls_back_when_session_removed, || async {
    let service = TestService::new()?;
    let mut watcher = service.new_watcher(Decodable::new_empty())?;
    let active_session_discovery = service
        .app
        .connect_to_service::<ActiveSessionMarker>()
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
});

// TODO(60352): deflake and re-enable.
#[ignore]
test!(inspect_tree_correct, || async {
    let mut service = TestService::new()?;
    let player1 = TestPlayer::new(&service).await?;
    let player2 = TestPlayer::new(&service).await?;

    let hierarchy = service.read_inspect().await;
    assert_eq!(hierarchy.children.len(), 1);
    let players = &hierarchy.children[0];
    assert_eq!(players.name, "players");
    assert_eq!(players.children.len(), 2);
    assert_eq!(players.children[0].name, format!("{}", player2.id));
    assert_eq!(players.children[1].name, format!("{}", player1.id));

    Ok(())
});
