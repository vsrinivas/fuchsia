// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use super::{
    filter::*,
    player_event::{PlayerEvent, SessionsWatcherEvent},
};
use crate::{Result, MAX_EVENTS_SENT_WITHOUT_ACK};
use fidl::client::QueryResponseFut;
use fidl_fuchsia_media_sessions2::*;
use futures::{
    self,
    future::{self, Ready},
    stream::FuturesOrdered,
    task::{Context, Poll},
    Sink, Stream,
};
use std::{collections::HashSet, pin::Pin};

/// Implements a sink to a client implementation of `fuchsia.media.sessions2.SessionsWatcher`.
///
/// Vends events to clients and provides back pressure when they have not ACKd already sent events.
pub struct FlowControlledProxySink {
    proxy: SessionsWatcherProxy,
    acks: FuturesOrdered<QueryResponseFut<()>>,
}

impl From<SessionsWatcherProxy> for FlowControlledProxySink {
    fn from(proxy: SessionsWatcherProxy) -> FlowControlledProxySink {
        FlowControlledProxySink { proxy, acks: FuturesOrdered::new() }
    }
}

impl Sink<(u64, SessionsWatcherEvent)> for FlowControlledProxySink {
    type Error = anyhow::Error;
    fn poll_ready(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<()>> {
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
        (id, event): (u64, SessionsWatcherEvent),
    ) -> Result<()> {
        let ack_fut = match event {
            SessionsWatcherEvent::Updated(delta) => self.proxy.session_updated(id, delta),
            SessionsWatcherEvent::Removed => self.proxy.session_removed(id),
        };
        self.acks.push(ack_fut);

        Ok(())
    }

    fn poll_flush(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<()>> {
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

    fn poll_close(self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Result<()>> {
        self.poll_flush(cx)
    }
}

pub fn watcher_filter(
    filter: Filter,
) -> impl FnMut(FilterApplicant<(u64, PlayerEvent)>) -> Ready<Option<(u64, SessionsWatcherEvent)>> {
    let mut allow_list = HashSet::new();

    move |event| {
        let allowed_now = filter.filter(&event);

        let (id, event) = event.applicant;
        let allowed_before = allow_list.contains(&id);

        future::ready(if allowed_now {
            allow_list.insert(id);
            Some((id, event.sessions_watcher_event()))
        } else if allowed_before {
            allow_list.remove(&id);

            // The client was watching this player, so we notify them it is
            // removed from their watch set.
            Some((id, PlayerEvent::Removed.sessions_watcher_event()))
        } else {
            None
        })
    }
}

#[cfg(test)]
mod test {
    use super::*;
    use crate::proxies::player::ValidPlayerInfoDelta;
    use fidl::{encoding::Decodable, endpoints::create_endpoints};
    use fuchsia_async as fasync;
    use futures::{stream, Future, SinkExt, StreamExt};
    use futures_test::task::*;
    use test_util::assert_matches;

    #[fasync::run_singlethreaded]
    #[test]
    async fn back_pressure_when_acks_behind() -> Result<()> {
        let (watcher_client, watcher_server) = create_endpoints::<SessionsWatcherMarker>()?;
        let mut under_test: FlowControlledProxySink = watcher_client.into_proxy()?.into();
        let mut watcher_requests = watcher_server.into_stream()?;

        let mut ctx = noop_context();
        let ready_when_empty = Pin::new(&mut under_test).poll_ready(&mut ctx);
        assert_matches!(ready_when_empty, Poll::Ready(Ok(())));

        let mut dummy_stream = stream::iter(
            (0..MAX_EVENTS_SENT_WITHOUT_ACK)
                .map(|_| Ok((0u64, PlayerEvent::Removed.sessions_watcher_event()))),
        );
        let mut send_all_fut = SinkExt::send_all(&mut under_test, &mut dummy_stream);
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
        let make_event = |player_id: u64, is_active| {
            FilterApplicant::new(
                WatchOptions { only_active: Some(is_active), ..Decodable::new_empty() },
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

        let mut dummy_stream =
            stream::iter((0u64..4u64).map(|i| make_event(i, false))).filter_map(watcher_filter(
                Filter::new(WatchOptions { only_active: Some(true), ..Decodable::new_empty() }),
            ));

        assert_matches!(dummy_stream.next().await, None);

        let mut dummy_stream =
            stream::iter((0u64..4u64).map(|i| make_event(i, true))).filter_map(watcher_filter(
                Filter::new(WatchOptions { only_active: Some(true), ..Decodable::new_empty() }),
            ));

        assert_matches!(dummy_stream.next().await, Some(_));

        Ok(())
    }
}
