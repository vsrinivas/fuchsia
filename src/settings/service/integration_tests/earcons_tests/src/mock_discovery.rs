// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::Error;
use fidl_fuchsia_media_sessions2::{
    DiscoveryRequest, DiscoveryRequestStream, SessionInfoDelta, SessionsWatcherProxy,
};
use fuchsia_async as fasync;
use fuchsia_component::server::{ServiceFs, ServiceFsDir};
use fuchsia_component_test::LocalComponentHandles;
use futures::channel::mpsc::Sender;
use futures::lock::Mutex;
use futures::SinkExt;
use futures::StreamExt;
use futures::TryStreamExt;
use std::sync::Arc;

pub type SessionId = u64;

/// Starts a mock discovery service.
///
/// `watchers` is a shared vec of all of registered [SessionsWatcherProxy] objects from watch calls
/// made to this API.
///
/// `watcher_sender` is an optional [Sender] that sends an event whenever a new watcher is
/// registered.
pub(crate) async fn discovery_service_mock(
    handles: LocalComponentHandles,
    watchers: Arc<Mutex<Vec<SessionsWatcherProxy>>>,
    watcher_sender: Sender<()>,
) -> Result<(), Error> {
    let mut fs = ServiceFs::new();
    let watchers = Arc::clone(&watchers);
    let _: &mut ServiceFsDir<'_, _> =
        fs.dir("svc").add_fidl_service(move |mut stream: DiscoveryRequestStream| {
            let watchers = Arc::clone(&watchers);
            let mut watcher_sender = watcher_sender.clone();
            fasync::Task::spawn(async move {
                while let Ok(Some(req)) = stream.try_next().await {
                    if let DiscoveryRequest::WatchSessions {
                        watch_options: _,
                        session_watcher,
                        control_handle: _,
                    } = req
                    {
                        if let Ok(proxy) = session_watcher.into_proxy() {
                            watchers.lock().await.push(proxy);
                            watcher_sender.send(()).await.expect("watch sent");
                        }
                    }
                }
            })
            .detach();
        });
    let _: &mut ServiceFs<_> = fs.serve_connection(handles.outgoing_dir).unwrap();
    fs.collect::<()>().await;
    Ok(())
}

/// Simulate updating a media session.
pub(crate) async fn update_session(
    watchers: Arc<Mutex<Vec<SessionsWatcherProxy>>>,
    id: SessionId,
    domain: &str,
) {
    for watcher in watchers.lock().await.iter() {
        watcher
            .session_updated(id, create_delta_with_domain(domain))
            .await
            .expect("Failed to send update request to handler");
    }
}

pub(crate) async fn remove_session(watchers: Arc<Mutex<Vec<SessionsWatcherProxy>>>, id: SessionId) {
    for watcher in watchers.lock().await.iter() {
        watcher.session_removed(id).await.expect("Failed to send remove request to handler");
    }
}

/// Create a fake SessionInfoDelta with the Domain set to |domain|.
fn create_delta_with_domain(domain: &str) -> SessionInfoDelta {
    SessionInfoDelta {
        domain: Some(domain.to_string()),
        is_local: Some(true),
        is_locally_active: Some(false),
        player_status: None,
        metadata: None,
        media_images: None,
        player_capabilities: None,
        ..SessionInfoDelta::EMPTY
    }
}
