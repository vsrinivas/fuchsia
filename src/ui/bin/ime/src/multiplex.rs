// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use anyhow::{format_err, Context as _, Error};
use fidl::endpoints::RequestStream;
use fidl_fuchsia_ui_text as txt;
use fuchsia_async as fasync;
use fuchsia_syslog::fx_log_err;
use futures::lock::Mutex;
use futures::prelude::*;
use std::convert::TryInto;
use std::sync::Arc;
use text::text_field_state::TextFieldState;

/// Multiplexes multiple TextFieldRequestStreams into a single TextFieldProxy. This is not quite as
/// easy as just forwarding each request — we need to broadcast events from the Proxy to every
/// client, and we also need to queue up transactions for each RequestStream so they don't
/// overlap with each other, and only forward the transaction all-at-once when CommitEdit is
/// called.
#[derive(Clone)]
pub struct TextFieldMultiplexer {
    inner: Arc<Mutex<TextFieldMultiplexerState>>,
}
struct TextFieldMultiplexerState {
    proxy: txt::TextFieldProxy,
    control_handles: Vec<txt::TextFieldControlHandle>,
    last_state: Option<TextFieldState>,
}

impl TextFieldMultiplexer {
    // TODO(fxbug.dev/26771): need way to close out a multiplexer's connections, right now Arc<Mutex<>> will
    // cause it to persist even if a new text field is focused.
    pub fn new(proxy: txt::TextFieldProxy) -> TextFieldMultiplexer {
        let mut event_stream = proxy.take_event_stream();
        let state =
            TextFieldMultiplexerState { proxy, control_handles: Vec::new(), last_state: None };
        let multiplexer = TextFieldMultiplexer { inner: Arc::new(Mutex::new(state)) };
        let multiplexer2 = multiplexer.clone();
        fasync::Task::spawn(
            async move {
                while let Some(msg) = event_stream
                    .try_next()
                    .await
                    .context("error reading value from text field event stream")?
                {
                    let txt::TextFieldEvent::OnUpdate { state: textfield_state } = msg;
                    match textfield_state.try_into() {
                        Ok(textfield_state) => {
                            let textfield_state: TextFieldState = textfield_state;
                            let mut multiplex_state = multiplexer2.inner.lock().await;
                            multiplex_state.control_handles.retain(|handle| {
                                handle.send_on_update(textfield_state.clone().into()).is_ok()
                            });
                            multiplex_state.last_state = Some(textfield_state);
                        }
                        Err(e) => {
                            fx_log_err!("failed to convert: {:?}", e);
                        }
                    }
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("{:?}", e)),
        )
        .detach();

        multiplexer
    }

    pub fn add_request_stream(&self, mut stream: txt::TextFieldRequestStream) {
        let this = self.clone();
        fasync::Task::spawn(
            async move {
                {
                    let mut multiplex_state = this.inner.lock().await;
                    let handle = stream.control_handle();
                    if let Some(textfield_state) = &multiplex_state.last_state {
                        // We already got at least one update, so send initial OnUpdate with current
                        // state to new TextField
                        let ok = handle.send_on_update(textfield_state.clone().into()).is_ok();
                        if !ok {
                            return Err(format_err!("Channel was closed"));
                        }
                    }
                    multiplex_state.control_handles.push(handle);
                }

                let mut edit_queue = Vec::new();
                let mut transaction_revision: Option<u64> = None;

                while let Some(req) = stream
                    .try_next()
                    .await
                    .context("error reading value from text field request stream")?
                {
                    this.handle_request(req, &mut edit_queue, &mut transaction_revision).await?;
                }
                Ok(())
            }
            .unwrap_or_else(|e: anyhow::Error| fx_log_err!("{:?}", e)),
        )
        .detach();
    }

    async fn handle_request<'a>(
        &'a self,
        req: txt::TextFieldRequest,
        edit_queue: &'a mut Vec<txt::TextFieldRequest>,
        transaction_revision: &'a mut Option<u64>,
    ) -> Result<(), Error> {
        let state = self.inner.lock().await;
        match req {
            req @ txt::TextFieldRequest::Replace { .. }
            | req @ txt::TextFieldRequest::SetSelection { .. }
            | req @ txt::TextFieldRequest::SetComposition { .. }
            | req @ txt::TextFieldRequest::ClearComposition { .. }
            | req @ txt::TextFieldRequest::SetDeadKeyHighlight { .. }
            | req @ txt::TextFieldRequest::ClearDeadKeyHighlight { .. } => {
                if transaction_revision.is_some() {
                    edit_queue.push(req);
                }
            }
            txt::TextFieldRequest::PositionOffset {
                mut old_position,
                offset,
                revision,
                responder,
            } => {
                let (mut position, error) =
                    state.proxy.position_offset(&mut old_position, offset, revision).await?;
                responder.send(&mut position, error)?;
            }
            txt::TextFieldRequest::Distance { mut range, revision, responder } => {
                let (distance, error) = state.proxy.distance(&mut range, revision).await?;
                responder.send(distance, error)?;
            }
            txt::TextFieldRequest::Contents { mut range, revision, responder } => {
                let (mut contents, mut point, error) =
                    state.proxy.contents(&mut range, revision).await?;
                responder.send(&mut contents, &mut point, error)?;
            }
            txt::TextFieldRequest::BeginEdit { revision, .. } => {
                *transaction_revision = Some(revision);
                edit_queue.clear();
            }
            txt::TextFieldRequest::CommitEdit { responder } => {
                if let Some(revision) = *transaction_revision {
                    state.proxy.begin_edit(revision)?;
                    for edit in edit_queue.iter_mut() {
                        forward_edit(edit, &state.proxy)?;
                    }
                    edit_queue.clear();
                    let error = state.proxy.commit_edit().await?;
                    responder.send(error)?;
                } else {
                    responder.send(txt::Error::BadRequest)?;
                }
                *transaction_revision = None;
            }
            txt::TextFieldRequest::AbortEdit { .. } => {
                *transaction_revision = None;
                edit_queue.clear();
            }
        }
        Ok(())
    }
}

fn forward_edit(msg: &mut txt::TextFieldRequest, proxy: &txt::TextFieldProxy) -> Result<(), Error> {
    match msg {
        txt::TextFieldRequest::Replace { ref mut range, ref mut new_text, .. } => {
            proxy.replace(range, new_text)?;
        }
        txt::TextFieldRequest::SetSelection { ref mut selection, .. } => {
            proxy.set_selection(selection)?;
        }
        txt::TextFieldRequest::SetComposition {
            ref mut composition_range,
            ref mut highlight_range,
            ..
        } => {
            proxy.set_composition(composition_range, highlight_range.as_deref_mut())?;
        }
        txt::TextFieldRequest::ClearComposition { .. } => {
            proxy.clear_composition()?;
        }
        txt::TextFieldRequest::SetDeadKeyHighlight { ref mut range, .. } => {
            proxy.set_dead_key_highlight(range)?;
        }
        txt::TextFieldRequest::ClearDeadKeyHighlight { .. } => {
            proxy.clear_dead_key_highlight()?;
        }
        _ => panic!("attempted to forward non-edit TextFieldRequest"),
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;
    use futures::future::join;

    async fn setup() -> (txt::TextFieldRequestStream, txt::TextFieldProxy, txt::TextFieldProxy) {
        let (proxy, server_end) = fidl::endpoints::create_proxy::<txt::TextFieldMarker>()
            .expect("failed to create TextFieldProxy");
        let multiplex = TextFieldMultiplexer::new(proxy);
        let final_request_stream =
            server_end.into_stream().expect("failed to create TextFieldRequestStream");

        let proxy1 = {
            let (client_end, request_stream) =
                fidl::endpoints::create_request_stream::<txt::TextFieldMarker>()
                    .expect("failed to create TextFieldRequestStream");
            multiplex.add_request_stream(request_stream);
            client_end.into_proxy().expect("failed to create TextFieldProxy")
        };

        let proxy2 = {
            let (client_end, request_stream) =
                fidl::endpoints::create_request_stream::<txt::TextFieldMarker>()
                    .expect("failed to create TextFieldRequestStream");
            multiplex.add_request_stream(request_stream);
            client_end.into_proxy().expect("failed to create TextFieldProxy")
        };

        (final_request_stream, proxy1, proxy2)
    }

    async fn get_stream_msg(stream: &mut txt::TextFieldRequestStream) -> txt::TextFieldRequest {
        stream
            .try_next()
            .await
            .expect("error reading value from stream")
            .expect("tried to read value from closed stream")
    }

    async fn expect_begin_edit(
        mut stream: &mut txt::TextFieldRequestStream,
        expected_revision: u64,
    ) {
        match get_stream_msg(&mut stream).await {
            txt::TextFieldRequest::BeginEdit { revision, .. } => {
                assert_eq!(revision, expected_revision);
            }
            _ => panic!("server got unexpected request!"),
        }
    }

    async fn expect_replace(mut stream: &mut txt::TextFieldRequestStream, expected_id: u64) {
        match get_stream_msg(&mut stream).await {
            txt::TextFieldRequest::Replace { range, .. } => {
                assert_eq!(range.start.id, expected_id);
            }
            _ => panic!("server got unexpected request!"),
        }
    }

    async fn expect_commit_edit(mut stream: &mut txt::TextFieldRequestStream) {
        match get_stream_msg(&mut stream).await {
            txt::TextFieldRequest::CommitEdit { responder, .. } => {
                responder.send(txt::Error::Ok).expect("failed to send CommitEdit reply");
            }
            _ => panic!("server got unexpected request!"),
        }
    }

    async fn expect_position_offset(mut stream: &mut txt::TextFieldRequestStream) {
        match get_stream_msg(&mut stream).await {
            txt::TextFieldRequest::PositionOffset { mut old_position, responder, .. } => {
                responder
                    .send(&mut old_position, txt::Error::Ok)
                    .expect("failed to send PositionOffset reply");
            }
            _ => panic!("server got unexpected request!"),
        }
    }

    #[fasync::run_until_stalled(test)]
    async fn forwards_content_requests_correctly() {
        let (mut stream, proxy_a, proxy_b) = setup().await;

        fasync::Task::spawn(async move {
            loop {
                expect_position_offset(&mut stream).await;
            }
        })
        .detach();

        let position_a = async move {
            let (position, _err) = proxy_a
                .position_offset(&mut txt::Position { id: 123 }, 0, 0)
                .await
                .expect("failed to call PositionOffset");
            assert_eq!(position.id, 123);
        };
        let position_b = async move {
            let (position, _err) = proxy_b
                .position_offset(&mut txt::Position { id: 321 }, 0, 0)
                .await
                .expect("failed to call PositionOffset");
            assert_eq!(position.id, 321);
        };
        join(position_a, position_b).await;
    }

    #[fasync::run_until_stalled(test)]
    async fn queues_interleaving_edits_correctly() {
        let (mut stream, proxy_a, proxy_b) = setup().await;

        fasync::Task::spawn(async move {
            expect_position_offset(&mut stream).await;

            expect_begin_edit(&mut stream, 0).await;
            expect_replace(&mut stream, 1).await;
            expect_replace(&mut stream, 3).await;
            expect_commit_edit(&mut stream).await;

            expect_begin_edit(&mut stream, 1).await;
            expect_replace(&mut stream, 2).await;
            expect_replace(&mut stream, 4).await;
            expect_commit_edit(&mut stream).await;
        })
        .detach();

        fn make_range(i: u64) -> txt::Range {
            txt::Range { start: txt::Position { id: i }, end: txt::Position { id: i } }
        }
        proxy_a.begin_edit(0).unwrap();
        proxy_b.begin_edit(1).unwrap();
        proxy_a.replace(&mut make_range(1), "").unwrap();
        proxy_b.replace(&mut make_range(2), "").unwrap();
        proxy_a.replace(&mut make_range(3), "").unwrap();
        proxy_b.replace(&mut make_range(4), "").unwrap();

        // position offset should be delivered first, before any commits
        let _ = proxy_a
            .position_offset(&mut txt::Position { id: 123 }, 0, 0)
            .await
            .expect("failed to call PositionOffset");
        // commit should succeed
        assert_eq!(proxy_a.commit_edit().await.expect("failed to send CommitEdit"), txt::Error::Ok);
        // but a second commit with no request should fail, and not even send something to the
        // TextField server
        assert_eq!(
            proxy_a.commit_edit().await.expect("failed to send CommitEdit"),
            txt::Error::BadRequest
        );
        // and a third commit, this time on the other proxy, should succeed
        assert_eq!(proxy_b.commit_edit().await.expect("failed to send CommitEdit"), txt::Error::Ok);
    }
}
