// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{
    filter::*,
    player_event::{PlayerEvent, SessionsWatcherEvent},
};
use crate::{Result, MAX_EVENTS_SENT_WITHOUT_ACK};
use failure::Error;
use fidl::client::QueryResponseFut;
use fidl_fuchsia_media_sessions2::*;
use futures::{
    self,
    stream::FuturesOrdered,
    task::{Context, Poll},
    Sink, Stream,
};
use std::{collections::HashSet, pin::Pin};

/// Implements a sink to a client implementation of `fuchsia.media.sessions2.SessionsWatcher`.
///
/// Vends events to clients and provides back pressure when they have not ACKd already sent events.
pub struct WatcherSink {
    filter: Filter,
    proxy: SessionsWatcherProxy,
    acks: FuturesOrdered<QueryResponseFut<()>>,
    allow_list: HashSet<u64>,
}

impl WatcherSink {
    pub fn new(filter: Filter, proxy: SessionsWatcherProxy) -> Self {
        Self { filter, proxy, acks: FuturesOrdered::new(), allow_list: HashSet::new() }
    }
}

impl Sink<FilterApplicant<(u64, PlayerEvent)>> for WatcherSink {
    type Error = Error;
    fn poll_ready(mut self: Pin<&mut Self>, cx: &mut Context) -> Poll<Result<()>> {
        if self.acks.len() < MAX_EVENTS_SENT_WITHOUT_ACK {
            return Poll::Ready(Ok(()));
        }

        match Pin::new(&mut self.acks).poll_next(cx) {
            Poll::Pending => Poll::Pending,
            Poll::Ready(None) | Poll::Ready(Some(Ok(_))) => {
                // We are now below the ACK limit and can send another event.
                Poll::Ready(Ok(()))
            }
            Poll::Ready(Some(Err(e))) => Poll::Ready(Err(e.into())),
        }
    }

    fn start_send(
        mut self: Pin<&mut Self>,
        event: FilterApplicant<(u64, PlayerEvent)>,
    ) -> Result<()> {
        let (id, _) = &event.applicant;
        let allowed_before = self.allow_list.contains(id);
        let allowed_now = self.filter.filter(&event);

        let (id, event) = if allowed_now {
            self.allow_list.insert(*id);
            event.applicant
        } else if allowed_before {
            self.allow_list.remove(id);

            // The client was watching this player, so we notify them it is
            // removed from their watch set.
            (*id, PlayerEvent::Removed)
        } else {
            return Ok(());
        };

        let ack = match event.sessions_watcher_event() {
            SessionsWatcherEvent::Updated(delta) => self.proxy.session_updated(id, delta),
            SessionsWatcherEvent::Removed => self.proxy.session_removed(id),
        };
        self.acks.push(ack);

        Ok(())
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context) -> Poll<Result<()>> {
        while let Poll::Ready(Some(r)) = Pin::new(&mut self.acks).poll_next(cx) {
            if let Err(e) = r {
                return Poll::Ready(Err(e.into()));
            }
        }

        if self.acks.is_empty() {
            Poll::Ready(Ok(()))
        } else {
            Poll::Pending
        }
    }

    fn poll_close(self: Pin<&mut Self>, cx: &mut Context) -> Poll<Result<()>> {
        self.poll_flush(cx)
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::proxies::player::ValidPlayerInfoDelta;
    use fidl::{encoding::Decodable, endpoints::create_endpoints};
    use fuchsia_async as fasync;
    use futures::{stream, Future, SinkExt};
    use futures_test::task::*;
    use test_util::assert_matches;

    #[fasync::run_singlethreaded]
    #[test]
    async fn back_pressure_when_acks_behind() -> Result<()> {
        let (watcher_client, watcher_server) = create_endpoints::<SessionsWatcherMarker>()?;
        let mut under_test = WatcherSink::new(Filter::default(), watcher_client.into_proxy()?);
        let mut watcher_requests = watcher_server.into_stream()?;

        let mut ctx = noop_context();
        let ready_when_empty = Pin::new(&mut under_test).poll_ready(&mut ctx);
        assert_matches!(ready_when_empty, Poll::Ready(Ok(())));

        let mut dummy_stream = stream::iter((0..MAX_EVENTS_SENT_WITHOUT_ACK).map(|_| {
            Ok(FilterApplicant::new(Decodable::new_empty(), (0u64, PlayerEvent::Removed)))
        }));
        let mut send_all_fut = under_test.send_all(&mut dummy_stream);
        let mut ack_responders = vec![];
        while ack_responders.len() < MAX_EVENTS_SENT_WITHOUT_ACK {
            let _ = Pin::new(&mut send_all_fut).poll(&mut ctx);
            match Pin::new(&mut watcher_requests).poll_next(&mut ctx) {
                Poll::Ready(Some(Ok(responder))) => ack_responders.push(
                    responder.into_session_removed().expect("Taking out removal event we sent").1,
                ),
                Poll::Ready(e) => panic!("Expected request stream to continue; got {:?}", e),
                _ => {}
            };
        }

        let ready_when_full_of_acks = Pin::new(&mut under_test).poll_ready(&mut ctx);
        assert_matches!(ready_when_full_of_acks, Poll::Pending);

        Ok(())
    }

    #[fasync::run_singlethreaded]
    #[test]
    async fn player_filter() -> Result<()> {
        let (watcher_client, watcher_server) = create_endpoints::<SessionsWatcherMarker>()?;
        let mut under_test = WatcherSink::new(
            Filter::new(WatchOptions { only_active: Some(true) }),
            watcher_client.into_proxy()?,
        );
        let mut watcher_requests = watcher_server.into_stream()?;

        let make_event = |player_id, is_active| {
            FilterApplicant::new(
                WatchOptions { only_active: Some(is_active) },
                (
                    player_id,
                    PlayerEvent::Updated {
                        delta: ValidPlayerInfoDelta::default(),
                        registration: None,
                        active: None,
                    },
                ),
            )
        };

        let allowed_player_event = make_event(1, true);
        let mut ctx = noop_context();
        let mut send_fut = under_test.send(allowed_player_event);
        loop {
            let _ = Pin::new(&mut send_fut).poll(&mut ctx);
            match Pin::new(&mut watcher_requests).poll_next(&mut ctx) {
                Poll::Ready(Some(Ok(SessionsWatcherRequest::SessionUpdated {
                    responder, ..
                }))) => {
                    responder.send()?;
                    break;
                }
                Poll::Ready(e) => panic!("Expected our watcher request; got {:?}", e),
                Poll::Pending => {}
            };
        }
        send_fut.await?;

        // We expect this to generate no event for the client.
        let disallowed_player_event = make_event(2, false);
        let send_disallowed_player_result = under_test.send(disallowed_player_event).await;
        // This should complete without our need to respond because it will never reach the client.
        assert_matches!(send_disallowed_player_result, Ok(_));

        let allowed_player_becomes_disallowed_event = make_event(1, false);
        let mut send_fut = under_test.send(allowed_player_becomes_disallowed_event);
        loop {
            let _ = Pin::new(&mut send_fut).poll(&mut ctx);
            match Pin::new(&mut watcher_requests).poll_next(&mut ctx) {
                Poll::Ready(Some(Ok(SessionsWatcherRequest::SessionRemoved {
                    session_id,
                    responder,
                }))) => {
                    assert_eq!(session_id, 1);
                    responder.send()?;
                    break;
                }
                Poll::Ready(e) => panic!("Expected our watcher request; got {:?}", e),
                Poll::Pending => {}
            };
        }
        send_fut.await?;

        Ok(())
    }
}
