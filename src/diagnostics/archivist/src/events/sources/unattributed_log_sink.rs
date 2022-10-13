// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use crate::{
    events::{
        error::EventError,
        router::{Dispatcher, EventProducer},
        types::{Event, EventPayload, LogSinkRequestedPayload},
    },
    identity::ComponentIdentity,
};
use fidl_fuchsia_logger as flogger;
use fuchsia_zircon as zx;
use futures::{channel::mpsc, StreamExt};

pub struct UnattributedLogSinkSource {
    dispatcher: Dispatcher,
    sender: mpsc::Sender<flogger::LogSinkRequestStream>,
    receiver: mpsc::Receiver<flogger::LogSinkRequestStream>,
}

impl Default for UnattributedLogSinkSource {
    fn default() -> Self {
        let (sender, receiver) = mpsc::channel(10);
        Self { sender, receiver, dispatcher: Dispatcher::default() }
    }
}

impl UnattributedLogSinkSource {
    pub fn publisher(&self) -> mpsc::Sender<flogger::LogSinkRequestStream> {
        self.sender.clone()
    }

    pub async fn spawn(mut self) -> Result<(), EventError> {
        while let Some(stream) = self.receiver.next().await {
            if let Err(err) = self
                .dispatcher
                .emit(Event {
                    timestamp: zx::Time::get_monotonic(),
                    payload: EventPayload::LogSinkRequested(LogSinkRequestedPayload {
                        component: ComponentIdentity::unknown(),
                        request_stream: Some(stream),
                    }),
                })
                .await
            {
                if err.is_disconnected() {
                    break;
                }
            }
        }
        Ok(())
    }
}

impl EventProducer for UnattributedLogSinkSource {
    fn set_dispatcher(&mut self, dispatcher: Dispatcher) {
        self.dispatcher = dispatcher;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::events::types::*;
    use fidl_fuchsia_logger::LogSinkMarker;
    use fuchsia_async as fasync;
    use futures::{SinkExt, StreamExt};
    use std::collections::BTreeSet;

    #[fuchsia::test]
    async fn events_have_unknown_identity() {
        let events = BTreeSet::from([EventType::LogSinkRequested]);
        let (mut event_stream, dispatcher) = Dispatcher::new_for_test(events);
        let mut source = UnattributedLogSinkSource::default();
        source.set_dispatcher(dispatcher);
        let mut publisher = source.publisher();
        let _task = fasync::Task::spawn(async move {
            source.spawn().await.unwrap();
        });
        let (_, log_sink_stream) =
            fidl::endpoints::create_proxy_and_stream::<LogSinkMarker>().unwrap();
        publisher.send(log_sink_stream).await.expect("send stream");

        let event = event_stream.next().await.expect("received event");
        let expected_identity = ComponentIdentity {
            url: "fuchsia-pkg://UNKNOWN".to_string(),
            instance_id: Some("0".to_string()),
            relative_moniker: vec!["UNKNOWN"].into(),
        };
        match event.payload {
            EventPayload::LogSinkRequested(LogSinkRequestedPayload {
                component,
                request_stream: Some(_),
            }) => {
                assert_eq!(component, expected_identity);
            }
            payload => unreachable!("{:?} never gets here", payload),
        }
    }
}
