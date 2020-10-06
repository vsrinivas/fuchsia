// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::tests::fakes::base::Service;
use anyhow::{format_err, Error};
use fidl::endpoints::{ServerEnd, ServiceMarker};
use fidl_fuchsia_media_sessions2::{
    DiscoveryMarker, DiscoveryRequest, SessionInfoDelta, SessionsWatcherProxy,
};
use fuchsia_async as fasync;
use fuchsia_zircon as zx;
use futures::lock::Mutex;
use futures::TryStreamExt;
use std::sync::Arc;

pub type SessionId = u64;

/// An implementation of the Discovery connection state manager for tests.
pub struct DiscoveryService {
    // Fake hanging get handler. Allows the service to mock the return of the watch
    // on a connection change.
    watchers: Arc<Mutex<Vec<SessionsWatcherProxy>>>,
}

impl DiscoveryService {
    pub fn new() -> Self {
        Self { watchers: Arc::new(Mutex::new(Vec::<SessionsWatcherProxy>::new())) }
    }

    /// Simulate updating a media session.
    pub async fn update_session(&self, id: SessionId, domain: &str) {
        for watcher in self.watchers.lock().await.iter() {
            watcher
                .session_updated(id, create_delta_with_domain(domain.clone()))
                .await
                .expect("Failed to send update request to handler");
        }
    }

    pub async fn remove_session(&self, id: SessionId) {
        for watcher in self.watchers.lock().await.iter() {
            watcher.session_removed(id).await.expect("Failed to send remove request to handler");
        }
    }
}

impl Service for DiscoveryService {
    fn can_handle_service(&self, service_name: &str) -> bool {
        return service_name == DiscoveryMarker::NAME;
    }

    fn process_stream(&mut self, service_name: &str, channel: zx::Channel) -> Result<(), Error> {
        if !self.can_handle_service(service_name) {
            return Err(format_err!("unsupported"));
        }

        let mut bluetooth_stream = ServerEnd::<DiscoveryMarker>::new(channel).into_stream()?;
        let watchers = self.watchers.clone();
        fasync::Task::spawn(async move {
            while let Some(req) = bluetooth_stream.try_next().await.unwrap() {
                match req {
                    DiscoveryRequest::WatchSessions {
                        watch_options: _,
                        session_watcher,
                        control_handle: _,
                    } => {
                        if let Ok(proxy) = session_watcher.into_proxy() {
                            watchers.lock().await.push(proxy);
                        }
                    }
                    _ => {}
                }
            }
        })
        .detach();

        Ok(())
    }
}

/// Create a fake SessionInfoDelta with the Domain set to |domain|.
fn create_delta_with_domain(domain: &str) -> SessionInfoDelta {
    return SessionInfoDelta {
        domain: Some(domain.to_string()),
        is_local: Some(true),
        is_locally_active: Some(false),
        player_status: None,
        metadata: None,
        media_images: None,
        player_capabilities: None,
    };
}
