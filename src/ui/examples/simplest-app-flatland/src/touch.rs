// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::internal_message::*,
    fidl_fuchsia_ui_pointer::{
        self as fptr, TouchEvent, TouchInteractionId, TouchInteractionStatus, TouchResponse,
    },
    fuchsia_async as fasync,
    futures::channel::mpsc::UnboundedSender,
    std::{collections::HashMap, slice::Iter},
    tracing::*,
};

/// Generate a vector of responses to the input `TouchEvents`, as required by
/// `fuchsia.ui.pointer.TouchSource.Watch()`.
fn generate_touch_event_responses(events: Iter<'_, TouchEvent>) -> Vec<TouchResponse> {
    events
        .map(|evt| {
            let mut response = TouchResponse::EMPTY;
            if evt.pointer_sample.is_some() {
                response.response_type = Some(fptr::TouchResponseType::Yes);
            }
            response
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
                        let trace_id = e.trace_flow_id.expect("Trace flow id should exist");

                        // Handle `pointer_sample` field, if it exists.
                        if let Some(fptr::TouchPointerSample {
                            interaction: Some(id),
                            phase: Some(phase),
                            ..
                        }) = &e.pointer_sample
                        {
                            ensure_interaction_exists(&mut interactions, &id);
                            let interaction = interactions.get_mut(&id).unwrap();
                            match interaction.status {
                                None => {
                                    // Buffer events until the interaction is granted or denied.
                                    interaction.buffered_messages.push(
                                        InternalMessage::TouchEvent {
                                            trace_id: trace_id.into(),
                                            phase: *phase,
                                        },
                                    );
                                }
                                Some(TouchInteractionStatus::Granted) => {
                                    // Samples received after the interaction is granted are
                                    // immediately sent to the app.
                                    sender
                                        .unbounded_send(InternalMessage::TouchEvent {
                                            phase: *phase,
                                            trace_id: trace_id.into(),
                                        })
                                        .expect("Failed to send internal message");
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
                            std::mem::swap(
                                &mut buffered_messages,
                                &mut interaction.buffered_messages,
                            );
                            match status {
                                fptr::TouchInteractionStatus::Granted => {
                                    for msg in buffered_messages {
                                        sender
                                            .unbounded_send(msg)
                                            .expect("Failed to send internal message")
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
                    error!("TouchSource connection closed");
                    return;
                }
            }
        }
    })
    .detach();
}
