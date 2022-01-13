// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::internal_message::*,
    fidl_fuchsia_ui_pointer::{
        self as fptr, TouchEvent, TouchInteractionId, TouchInteractionStatus, TouchResponse,
    },
    fuchsia_async as fasync,
    fuchsia_syslog::{fx_log_err, fx_log_info},
    futures::channel::mpsc::UnboundedSender,
    std::{collections::HashMap, slice::Iter},
};

/// Generate a vector of responses to the input `TouchEvents`, as required by
/// `fuchsia.ui.pointer.TouchSource.Watch()`.
fn generate_touch_event_responses(events: Iter<'_, TouchEvent>) -> Vec<TouchResponse> {
    events
        .map(|evt| {
            if let Some(_) = &evt.pointer_sample {
                return TouchResponse {
                    response_type: Some(fptr::TouchResponseType::Yes),
                    trace_flow_id: evt.trace_flow_id,
                    ..TouchResponse::EMPTY
                };
            }
            TouchResponse::EMPTY
        })
        .collect()
}

/// This is a very simplistic touch event handler, which:
/// - is always interested in claiming events, i.e. uses fuchsia.ui.pointer.TouchResponseType.YES
///   instead of one of the other types.
/// - uses an `UnboundedSender` to send events to the main application logic.  These events are
///   buffered until the status is fuchsia.ui.pointer.TouchInteractionStatus::GRANTED.
///   Subsequent events in the same interaction are sent immediately, not buffered.
pub fn spawn_touch_source_watcher(
    touch_source: fptr::TouchSourceProxy,
    sender: UnboundedSender<InternalMessage>,
) {
    fasync::Task::spawn(async move {
        // Each time a client calls the `fuchsia.ui.pointer.TouchSource.Watch()` hanging get method,
        // they are required to provide responses to all of the events received from the previous
        // call to `Watch()`.  This variable holds these responses, and is initially empty because
        // there was no previous call to `Watch()`.
        let mut pending_responses: Vec<TouchResponse> = vec![];

        // An interaction is a add-change-remove sequence for a single "finger" on a particular
        // device.  Until the interaction status has been settled (i.e. the entire interaction is
        // either denied or granted), events are buffered.  When the interaction is granted, the
        // buffered events are sent to the app via `sender`, and subsequent events are immediately
        // sent via `sender`.  Conversely, when the interaction is denied, buffered events and all
        // subsequent events are dropped.
        struct Interaction {
            // Only contains InternalMessage::TouchEvents.
            buffered_messages: Vec<InternalMessage>,
            status: Option<fptr::TouchInteractionStatus>,
        }
        let mut interactions: HashMap<TouchInteractionId, Interaction> = HashMap::new();

        // If no `Interaction` exists for the specified `id`, insert a newly-instantiated one.
        fn ensure_interaction_exists(
            map: &mut HashMap<TouchInteractionId, Interaction>,
            id: &TouchInteractionId,
        ) {
            if !map.contains_key(id) {
                map.insert(id.clone(), Interaction { buffered_messages: vec![], status: None });
            }
        }

        loop {
            let events = touch_source.watch(&mut pending_responses.into_iter());

            match events.await {
                Ok(events) => {
                    // Generate the responses which will be sent with the next call to
                    // `fuchsia.ui.pointer.TouchSource.Watch()`.
                    pending_responses = generate_touch_event_responses(events.iter());

                    for e in events.iter() {
                        let timestamp = e.timestamp.unwrap();

                        // Handle `pointer_sample` field, if it exists.
                        if let Some(fptr::TouchPointerSample {
                            interaction: Some(id),
                            phase: Some(phase),
                            position_in_viewport: Some(position_in_viewport),
                            ..
                        }) = &e.pointer_sample
                        {
                            ensure_interaction_exists(&mut interactions, &id);
                            let interaction = interactions.get_mut(&id).unwrap();
                            let msg = InternalMessage::TouchEvent {
                                timestamp,
                                interaction: id.clone(),
                                phase: phase.clone(),
                                position_in_viewport: position_in_viewport.clone(),
                            };
                            match interaction.status {
                                None => {
                                    // Buffer events until the interaction is granted or denied.
                                    interaction.buffered_messages.push(msg);
                                }
                                Some(TouchInteractionStatus::Granted) => {
                                    // Samples received after the interaction is granted are
                                    // immediately sent to the app.
                                    if let Err(e) = sender.unbounded_send(msg) {
                                        fx_log_err!("Failed to send TouchEvent message for granted interaction: {}", e);
                                        return;
                                    }
                                }
                                Some(TouchInteractionStatus::Denied) => {
                                    // Drop the event/msg, and remove the interaction from the map:
                                    // we're guaranteed not to receive any further events for this
                                    // interaction.
                                    interactions.remove(&id);
                                }
                            }
                        }

                        // Handle `interaction_result` field, if it exists.
                        if let Some(fptr::TouchInteractionResult { interaction: id, status }) =
                            &e.interaction_result
                        {
                            ensure_interaction_exists(&mut interactions, &id);
                            let interaction = interactions.get_mut(&id).unwrap();
                            if let Some(existing_status) = &interaction.status {
                                // The status of an interaction can only change from None to Some().
                                assert_eq!(status, existing_status);
                            } else {
                                // Status was previously None.
                                interaction.status = Some(status.clone());
                            }

                            // Grab any buffered events, and replace them with an empty vector.
                            let mut buffered_messages = vec![];
                            std::mem::swap(&mut buffered_messages, &mut interaction.buffered_messages);
                            match status {
                                fptr::TouchInteractionStatus::Granted => {
                                    for msg in buffered_messages {
                                        if let Err(e) = sender.unbounded_send(msg) {
                                            fx_log_info!("Failed to send TouchEvent message for newly-granted interaction: {}", e);
                                            return;
                                        }
                                    }
                                }
                                fptr::TouchInteractionStatus::Denied => {
                                    // Drop any buffered events and remove the interaction from the
                                    // map: we're guaranteed not to receive any further events for
                                    // this interaction.
                                    interactions.remove(&id);
                                }
                            }
                        }
                    }
                }
                _ => {
                    fx_log_info!("TouchSource connection closed");
                    return;
                }
            }
        }
    })
    .detach();
}

#[cfg(test)]
mod tests {
    use {
        crate::internal_message::*,
        anyhow::anyhow,
        fidl::endpoints::create_proxy_and_stream,
        fidl_fuchsia_ui_pointer::{
            self as fptr, EventPhase, TouchEvent, TouchInteractionId, TouchInteractionStatus,
        },
        fuchsia_async as fasync,
        futures::{channel::mpsc::unbounded, StreamExt, TryStreamExt},
    };

    // Handles `Watch()` requests from the stream by iterating over the responses.  Does only
    // minimal validation of the request arguments.  Specifically, only checks that the correct
    // number of responses is sent by the client.
    //
    // Returns the `request_stream` so that e.g. this function can be called again with different
    // events; this is useful for verifying that not-yet-granted events are buffered properly.
    async fn handle_touch_source_watch_requests(
        mut request_stream: fptr::TouchSourceRequestStream,
        responses: Vec<Vec<TouchEvent>>,
    ) -> Result<fptr::TouchSourceRequestStream, anyhow::Error> {
        // With each call to `Watch()` the client must pass a vector of `TouchResponse` equal in
        // size to the number of `TouchEvent` that they received in response to their previous call
        // to `Watch()`.  This number is initially zero, because there was no previous call to
        // `Watch()`.
        let mut expected_client_response_count: usize = 0;

        // For the purposes of validating the args to `Watch()`, we treat the first loop iteration
        // differently.  See comments below.
        let mut is_first_loop_iteration = true;

        // Each time we receive a `Watch()` request from the client, we respond with the next
        // response provided by the caller of this function.
        let mut response_iter = responses.iter();
        while let Some(events) = response_iter.next() {
            if let Ok(Some(request)) = request_stream.try_next().await {
                match request {
                    fptr::TouchSourceRequest::Watch { responses, responder } => {
                        // Verify that the number of responses matches the number of events sent in
                        // response to the *previous* call to Watch().
                        if is_first_loop_iteration {
                            // Some tests may want to call handle_touch_source_watch_requests() multiple
                            // times, and assert conditions after each batch of events has been sent.
                            // In such cases, we don't know which (if any) events were previously sent,
                            // so we can't verify that the client sent the correct number of responses.
                            is_first_loop_iteration = false;
                        } else {
                            assert_eq!(expected_client_response_count, responses.len());
                        }

                        expected_client_response_count = events.len();
                        responder.send(&mut events.clone().into_iter())?;
                    }
                    _ => {
                        return Err(anyhow!("unexpected request: only Watch() is supported"));
                    }
                }
            } else {
                return Err(anyhow!("not all responses were consumed by client"));
            }
        }
        Ok(request_stream)
    }

    fn make_touch_sample(
        timestamp: i64,
        interaction: &TouchInteractionId,
        phase: fptr::EventPhase,
        x: f32,
        y: f32,
    ) -> TouchEvent {
        let interaction = Some(interaction.clone());
        TouchEvent {
            timestamp: Some(timestamp),
            pointer_sample: Some(fptr::TouchPointerSample {
                interaction,
                phase: Some(phase),
                position_in_viewport: Some([x, y]),
                ..fptr::TouchPointerSample::EMPTY
            }),
            ..TouchEvent::EMPTY
        }
    }

    fn make_touch_interaction_result(
        timestamp: i64,
        interaction: &TouchInteractionId,
        status: fptr::TouchInteractionStatus,
    ) -> TouchEvent {
        let interaction = interaction.clone();
        TouchEvent {
            timestamp: Some(timestamp),
            interaction_result: Some(fptr::TouchInteractionResult { interaction, status }),
            ..TouchEvent::EMPTY
        }
    }

    fn assert_equality(msg: InternalMessage, event: TouchEvent) {
        if let InternalMessage::TouchEvent { timestamp, interaction, phase, position_in_viewport } =
            msg
        {
            assert_eq!(timestamp, event.timestamp.unwrap());

            if let Some(pointer_sample) = event.pointer_sample {
                assert_eq!(interaction, pointer_sample.interaction.unwrap());
                assert_eq!(phase, pointer_sample.phase.unwrap());
                assert_eq!(position_in_viewport, pointer_sample.position_in_viewport.unwrap());
            } else {
                panic!("TouchEvent does not have a TouchPointerSample");
            }
        } else {
            panic!("Message is not a InternalMessage::TouchEvent");
        }
    }

    #[fasync::run_singlethreaded(test)]
    async fn test_spawn_touch_source_watcher() -> Result<(), anyhow::Error> {
        let (touch_proxy, touch_stream) = create_proxy_and_stream::<fptr::TouchSourceMarker>()
            .expect("failed to create TouchSource proxy and stream");

        let (internal_sender, mut internal_receiver) = unbounded::<InternalMessage>();

        super::spawn_touch_source_watcher(touch_proxy, internal_sender);

        let interaction1 = TouchInteractionId { device_id: 1, pointer_id: 1, interaction_id: 1 };
        let interaction2 = TouchInteractionId { device_id: 1, pointer_id: 2, interaction_id: 1 };

        // Begin two interactions.
        let touch_stream = handle_touch_source_watch_requests(
            touch_stream,
            vec![
                vec![
                    make_touch_sample(0, &interaction1, EventPhase::Add, 100.0, 100.0),
                    make_touch_sample(1, &interaction1, EventPhase::Change, 120.0, 120.0),
                    make_touch_sample(2, &interaction1, EventPhase::Change, 140.0, 140.0),
                    make_touch_sample(3, &interaction1, EventPhase::Change, 160.0, 160.0),
                ],
                vec![
                    make_touch_sample(4, &interaction2, EventPhase::Add, 200.0, 200.0),
                    make_touch_sample(5, &interaction2, EventPhase::Change, 180.0, 220.0),
                    make_touch_sample(6, &interaction2, EventPhase::Change, 160.0, 240.0),
                    make_touch_sample(7, &interaction2, EventPhase::Change, 140.0, 260.0),
                ],
            ],
        )
        .await?;

        // No events were received because neither interaction was granted nor denied.
        assert!(internal_receiver.try_next().is_err());

        // Grant one interaction and deny the other.
        let touch_stream = handle_touch_source_watch_requests(
            touch_stream,
            vec![vec![
                make_touch_interaction_result(8, &interaction1, TouchInteractionStatus::Denied),
                make_touch_interaction_result(9, &interaction2, TouchInteractionStatus::Granted),
            ]],
        )
        .await?;

        // All of the events from `interaction1` were dropped because the interaction was denied.
        // All of the events from `interaction2` were previously buffered, but now that the
        // interaction is granted, they are all sent on the `InternalMessage` channel.
        assert_equality(
            internal_receiver.next().await.unwrap(),
            make_touch_sample(4, &interaction2, EventPhase::Add, 200.0, 200.0),
        );
        assert_equality(
            internal_receiver.next().await.unwrap(),
            make_touch_sample(5, &interaction2, EventPhase::Change, 180.0, 220.0),
        );
        assert_equality(
            internal_receiver.next().await.unwrap(),
            make_touch_sample(6, &interaction2, EventPhase::Change, 160.0, 240.0),
        );
        assert_equality(
            internal_receiver.next().await.unwrap(),
            make_touch_sample(7, &interaction2, EventPhase::Change, 140.0, 260.0),
        );

        // No more events are currently available.
        assert!(internal_receiver.try_next().is_err());

        // Receive more events from the touch source.
        let _touch_stream = handle_touch_source_watch_requests(
            touch_stream,
            vec![
                vec![
                    make_touch_sample(8, &interaction1, EventPhase::Change, 165.0, 155.0),
                    make_touch_sample(9, &interaction1, EventPhase::Change, 170.0, 150.0),
                    make_touch_sample(10, &interaction1, EventPhase::Change, 175.0, 145.0),
                    make_touch_sample(11, &interaction1, EventPhase::Remove, 180.0, 140.0),
                ],
                vec![
                    make_touch_sample(12, &interaction2, EventPhase::Change, 120.0, 280.0),
                    make_touch_sample(13, &interaction2, EventPhase::Change, 100.0, 300.0),
                    make_touch_sample(14, &interaction2, EventPhase::Change, 100.0, 320.0),
                    make_touch_sample(15, &interaction2, EventPhase::Remove, 100.0, 340.0),
                ],
            ],
        )
        .await?;

        // All of the events from `interaction1` were dropped because the interaction was denied.
        // All of the events from `interaction2` are sent on the `InternalMessage` channel as soon
        // as they are received; they are not buffered because the interaction has already been
        // granted (we cannot directly observe that there is no buffering, only that the events are
        // received as expected).
        assert_equality(
            internal_receiver.next().await.unwrap(),
            make_touch_sample(12, &interaction2, EventPhase::Change, 120.0, 280.0),
        );
        assert_equality(
            internal_receiver.next().await.unwrap(),
            make_touch_sample(13, &interaction2, EventPhase::Change, 100.0, 300.0),
        );
        assert_equality(
            internal_receiver.next().await.unwrap(),
            make_touch_sample(14, &interaction2, EventPhase::Change, 100.0, 320.0),
        );
        assert_equality(
            internal_receiver.next().await.unwrap(),
            make_touch_sample(15, &interaction2, EventPhase::Remove, 100.0, 340.0),
        );

        Ok(())
    }
}
