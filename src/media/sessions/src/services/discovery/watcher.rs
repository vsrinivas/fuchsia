// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::player_event::{PlayerEvent, SessionsWatcherEvent};
use crate::{mpmc, Result, CHANNEL_BUFFER_SIZE, MAX_EVENTS_SENT_WITHOUT_ACK};
use failure::ResultExt;
use fidl::{client::QueryResponseFut, endpoints::*};
use fidl_fuchsia_media_sessions2::*;
use futures::{self, channel::mpsc, prelude::*, stream::FuturesUnordered};
use std::collections::hash_map::*;

#[derive(Debug, Default, Clone, Copy)]
struct PlayerState {
    active: bool,
    local: bool,
}

impl PlayerState {
    /// Updates the filter state with the new event and returns whether it should be
    /// retained.
    fn update(&mut self, event: &PlayerEvent) {
        match event {
            PlayerEvent::Updated { delta, active, .. } => {
                self.active = active.unwrap_or(self.active);
                self.local = delta.local.unwrap_or(self.local);
            }
            PlayerEvent::Removed => {}
        }
    }
}

/// Implements `fuchsia.media.sessions2.SessionsWatcher`.
///
/// This struct serves a single request, `WatchSessions`, which returns tables representing deltas to
/// the state of the world since the last request. To enable this, `Watcher` keeps track of a set of
/// events it needs to send the client, and shares a view of the state of all registered players.
///
/// We need to keep clones of events in each instance of `Watcher` because clients may consume them
/// at different rates; each one needs a individual queue.
pub struct Watcher {
    staged: HashMap<u64, PlayerEvent>,
    players: HashMap<u64, PlayerState>,
    options: WatchOptions,
    event_stream: mpmc::Receiver<(u64, PlayerEvent)>,
}

impl Watcher {
    pub async fn new(
        options: WatchOptions,
        catch_up_events: HashMap<u64, PlayerEvent>,
        event_stream: mpmc::Receiver<(u64, PlayerEvent)>,
    ) -> Self {
        let players = catch_up_events
            .iter()
            .map(|(id, event)| {
                let mut filter = PlayerState::default();
                filter.update(event);
                (*id, filter)
            })
            .collect();
        Self { staged: catch_up_events, players, options, event_stream }
    }

    pub async fn serve(mut self, proxy: ClientEnd<SessionsWatcherMarker>) -> Result<()> {
        let proxy = proxy.into_proxy().context("Making SessionsWatcher request stream.")?;
        let mut acks = FuturesUnordered::new();
        let (mut emit_trigger, mut emit_signal) = mpsc::channel(CHANNEL_BUFFER_SIZE);

        emit_trigger.send(()).await?;

        // Loop until the proxy disconnects. Since we never close `emit`, we will never not have a
        // stream to poll in the select.
        loop {
            futures::select! {
                _ = acks.select_next_some() => {
                    emit_trigger.send(()).await?;
                }
                _ = emit_signal.select_next_some() => {
                    // TODO(turnage): Reject out-of-order ACKs
                    if acks.len() < MAX_EVENTS_SENT_WITHOUT_ACK {
                        let budget = MAX_EVENTS_SENT_WITHOUT_ACK - acks.len();
                        for ack in self.maybe_emit_events(&proxy, budget).await? {
                            acks.push(ack);
                        }
                    }
                }
                // A player has posted an update.
                tagged_event = self.event_stream.select_next_some() => {
                    let (id, new_event) = tagged_event;
                    let event = self.staged.remove(&id).unwrap_or_default();
                    let event = event.update(new_event);
                    if event.is_removal() {
                        self.players.remove(&id);
                    } else {
                        self.players.entry(id).or_default().update(&event);
                    }
                    self.staged.insert(id, event);
                    emit_trigger.send(()).await?;
                }
                complete => panic!("The emitter stream should never die."),
            }
        }
    }

    /// Emits events through the responder if any are eligible for emission. Returns the number of
    /// events sent without ACK.
    ///
    /// Events not emitted are retained, because the reason they are withheld from the client may
    /// expire (e.g. a client may be listening for only active sessions, and that session may become
    /// active at a later time, in which case we want to provide all the information we have so far).
    async fn maybe_emit_events(
        &mut self,
        proxy: &SessionsWatcherProxy,
        budget: usize,
    ) -> Result<Vec<QueryResponseFut<()>>> {
        let mut to_send = vec![];
        let ids: Vec<u64> = self.staged.keys().cloned().collect();
        for id in ids {
            let event = self.staged.remove(&id).expect("Getting value of key we just read");
            if self.filter_event(id, &event).await {
                to_send.push((id, event.sessions_watcher_event()));
            } else {
                self.staged.insert(id, event);
            }
            if to_send.len() == budget {
                break;
            }
        }

        let mut acks = vec![];
        for (id, event) in to_send {
            acks.push(match event {
                SessionsWatcherEvent::Updated(delta) => proxy.session_updated(id, delta),
                SessionsWatcherEvent::Removed => proxy.session_removed(id),
            });
        }

        Ok(acks)
    }

    /// This returns the event to caller only if it is of interest to the client, as determined by
    /// `WatchOptions` the client provided when creating this `Watcher`.
    async fn filter_event<'a>(&'a self, id: u64, event: &'a PlayerEvent) -> bool {
        let mut allowed = true;

        let player_filter_state = self.players.get(&id).cloned().unwrap_or_default();
        let player_removed = event.is_removal();

        let filter_for_only_active = self.options.only_active.unwrap_or(false);
        let update_to_active_status = event.updates_activity();

        allowed &= !filter_for_only_active
            || player_filter_state.active
            || update_to_active_status
            || player_removed;

        allowed
    }
}
