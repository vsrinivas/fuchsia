// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{
    filter::*,
    player_event::{PlayerEvent, SessionsWatcherEvent},
};
use crate::{Result, MAX_EVENTS_SENT_WITHOUT_ACK};
use failure::ResultExt;
use fidl::endpoints::*;
use fidl_fuchsia_media_sessions2::*;
use futures::{self, channel::mpsc, prelude::*};
use std::collections::{HashSet, VecDeque};

/// Implements `fuchsia.media.sessions2.SessionsWatcher`.
///
/// This struct serves a single request, `WatchSessions`, which returns tables representing deltas to
/// the state of the world since the last request. To enable this, `Watcher` keeps track of a set of
/// events it needs to send the client, and shares a view of the state of all registered players.
///
/// We need to keep clones of events in each instance of `Watcher` because clients may consume them
/// at different rates; each one needs a individual queue.
pub struct Watcher {
    filter: Filter,
    event_stream: mpsc::Receiver<FilterApplicant<(u64, PlayerEvent)>>,
}

impl Watcher {
    pub fn new(
        filter: Filter,
        event_stream: mpsc::Receiver<FilterApplicant<(u64, PlayerEvent)>>,
    ) -> Self {
        Self { filter, event_stream }
    }

    pub async fn serve(mut self, proxy: ClientEnd<SessionsWatcherMarker>) -> Result<()> {
        let proxy = proxy.into_proxy().context("Making SessionsWatcher request stream.")?;
        let mut acks = VecDeque::new();
        let mut included_sessions = HashSet::new();

        while let Some(event) = self.event_stream.next().await {
            let id = event.applicant.0;
            let (_, player_event) = if self.filter.filter(&event) {
                included_sessions.insert(id);
                event.applicant
            } else if included_sessions.contains(&id) {
                // When a session is no longer included in the filter set, we still emit the state
                // that removes it from the set so clients know it is gone.
                included_sessions.remove(&id);
                event.applicant
            } else {
                continue;
            };

            while acks.len() >= MAX_EVENTS_SENT_WITHOUT_ACK {
                acks.pop_front().unwrap().await?;
            }

            debug_assert!(acks.len() < MAX_EVENTS_SENT_WITHOUT_ACK);
            acks.push_back(match player_event.sessions_watcher_event() {
                SessionsWatcherEvent::Updated(delta) => proxy.session_updated(id, delta),
                SessionsWatcherEvent::Removed => proxy.session_removed(id),
            });
        }

        Ok(())
    }
}
