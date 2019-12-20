// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::Event,
    futures::{
        channel::{mpsc, oneshot},
        future::Future,
        lock::Mutex,
        stream::{Stream, TryStreamExt},
        task::{Context, Poll},
    },
    hyper::{Body, Chunk, Response, StatusCode},
    std::{mem::replace, ops::DerefMut, pin::Pin, sync::Arc},
};

pub struct SseResponseCreator {
    buffer_size: usize,
    clients: Arc<Mutex<Vec<Client>>>,
}

impl SseResponseCreator {
    /// hyper `Response` `Body`s created by this `SseResponseCreator` will buffer
    /// `buffer_size + 1` `Events` before the `Body` stream is closed for falling too far behind.
    pub fn with_additional_buffer_size(buffer_size: usize) -> (Self, EventSender) {
        let clients = Arc::new(Mutex::new(vec![]));
        (Self { buffer_size, clients: Arc::clone(&clients) }, EventSender { clients })
    }

    /// Creates hyper `Response`s whose `Body`s receive `Event`s from the `EventSender` associated
    /// with this `SseResponseCreator`.
    pub async fn create(&self) -> Response<Body> {
        let (abort_tx, abort_rx) = oneshot::channel();
        let (chunk_tx, chunk_rx) = mpsc::channel(self.buffer_size);
        self.clients.lock().await.push(Client { abort_tx, chunk_tx });
        Response::builder()
            .status(StatusCode::OK)
            .header("content-type", "text/event-stream")
            .body(Body::wrap_stream(BodyAbortStream { abort_rx, chunk_rx }.compat()))
            .unwrap() // builder arguments are all statically determined, build will not fail
    }
}

pub struct EventSender {
    clients: Arc<Mutex<Vec<Client>>>,
}

impl EventSender {
    /// Send an `Event` to each connected client. Clients that have fallen too far behind have
    /// their connections closed.
    pub async fn send(&self, event: &Event) {
        let mut clients_guard = self.clients.lock().await;
        let clients = replace(DerefMut::deref_mut(&mut clients_guard), vec![]);
        let clients = clients
            .into_iter()
            .filter_map(|mut c| {
                if c.try_send(event).is_ok() {
                    Some(c)
                } else {
                    let _ = c.abort();
                    None
                }
            })
            .collect();
        replace(DerefMut::deref_mut(&mut clients_guard), clients);
    }
}

// Reimplementation of the Body created by hyper::body::Body::channel() b/c in-tree hyper uses old futures API
struct BodyAbortStream {
    abort_rx: oneshot::Receiver<()>,
    chunk_rx: mpsc::Receiver<Chunk>,
}

impl Stream for BodyAbortStream {
    type Item = Result<Chunk, &'static str>;

    fn poll_next(mut self: Pin<&mut Self>, cx: &mut Context<'_>) -> Poll<Option<Self::Item>> {
        if let Poll::Ready(_) = Pin::new(&mut self.abort_rx).poll(cx) {
            return Poll::Ready(Some(Err("client dropped")));
        }
        match Pin::new(&mut self.chunk_rx).poll_next(cx) {
            Poll::Ready(Some(chunk)) => Poll::Ready(Some(Ok(chunk))),
            Poll::Ready(None) => Poll::Ready(None),
            Poll::Pending => Poll::Pending,
        }
    }
}

// Reimplementation of hyper::body::Sender b/c in-tree hyper uses old futures API
struct Client {
    abort_tx: oneshot::Sender<()>,
    chunk_tx: mpsc::Sender<Chunk>,
}

impl Client {
    fn try_send(&mut self, event: &Event) -> Result<(), ()> {
        self.chunk_tx.try_send(event.to_vec().into()).map_err(|_| ())
    }
    fn abort(self) {
        let _ = self.abort_tx.send(());
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fuchsia_async::{self as fasync},
        futures::{compat::Stream01CompatExt, stream::StreamExt},
        matches::assert_matches,
    };

    #[fasync::run_singlethreaded(test)]
    async fn response_headers() {
        let (sse_response_creator, _) = SseResponseCreator::with_additional_buffer_size(0);
        let resp = sse_response_creator.create().await;

        assert_eq!(resp.status(), StatusCode::OK);
        assert_eq!(
            resp.headers().get("content-type").map(|h| h.as_bytes()),
            Some(&b"text/event-stream"[..])
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn response_correct_body_single_event() {
        let event = Event::from_type_and_data("event_type", "data_contents").unwrap();
        let (sse_response_creator, event_sender) =
            SseResponseCreator::with_additional_buffer_size(0);
        let resp = sse_response_creator.create().await;

        event_sender.send(&event).await;
        let mut body_stream = resp.into_body().compat();
        let body_bytes = body_stream.next().await;

        assert_eq!(body_bytes.unwrap().unwrap().to_vec(), event.to_vec());
    }

    #[fasync::run_singlethreaded(test)]
    async fn full_client_dropped_other_clients_continue_to_receive_events() {
        let event0 = Event::from_type_and_data("event_type0", "data_contents0").unwrap();
        let (sse_response_creator, event_sender) =
            SseResponseCreator::with_additional_buffer_size(0);
        let mut body_stream0 = sse_response_creator.create().await.into_body().compat();
        let mut body_stream1 = sse_response_creator.create().await.into_body().compat();

        event_sender.send(&event0).await;

        let body_bytes1 = body_stream1.next().await;

        assert_matches!(body_bytes1, Some(Ok(chunk)) if chunk.to_vec() == event0.to_vec());

        let event1 = Event::from_type_and_data("event_type1", "data_contents1").unwrap();
        event_sender.send(&event1).await;

        let body_bytes0 = body_stream0.next().await;
        assert_matches!(body_bytes0, Some(Err(_)));

        let body_bytes1 = body_stream1.next().await;
        assert_eq!(body_bytes1.unwrap().unwrap().to_vec(), event1.to_vec());
    }
}
