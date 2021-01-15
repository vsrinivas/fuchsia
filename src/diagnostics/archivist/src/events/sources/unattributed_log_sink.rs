// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        container::ComponentIdentity,
        events::{error::EventError, types::*},
    },
    async_trait::async_trait,
    fidl_fuchsia_logger::LogSinkRequestStream,
    fuchsia_async as fasync,
    futures::{channel::mpsc, SinkExt, StreamExt},
};

pub struct UnattributedLogSinkSource {
    stream_sender: mpsc::Sender<LogSinkRequestStream>,
    stream_receiver: Option<mpsc::Receiver<LogSinkRequestStream>>,
}

impl UnattributedLogSinkSource {
    pub fn new() -> Self {
        let (stream_sender, receiver) = mpsc::channel(CHANNEL_CAPACITY);
        Self { stream_sender, stream_receiver: Some(receiver) }
    }

    pub fn get_publisher(&self) -> mpsc::Sender<LogSinkRequestStream> {
        self.stream_sender.clone()
    }
}

#[async_trait]
impl EventSource for UnattributedLogSinkSource {
    async fn listen(&mut self, mut sender: mpsc::Sender<ComponentEvent>) -> Result<(), EventError> {
        match self.stream_receiver.take() {
            None => Err(EventError::StreamAlreadyTaken),
            Some(receiver) => {
                fasync::Task::spawn(async move {
                    let mut stream = Box::pin(receiver.boxed());
                    while let Some(requests) = stream.next().await {
                        let _ = sender
                            .send(ComponentEvent::LogSinkRequested(LogSinkRequestedEvent {
                                metadata: EventMetadata::new(ComponentIdentity::unknown()),
                                requests,
                            }))
                            .await;
                    }
                })
                .detach();
                Ok(())
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {super::*, fidl, fidl_fuchsia_logger::LogSinkMarker, matches::assert_matches};

    #[fasync::run_singlethreaded(test)]
    async fn events_have_unknown_identity() {
        let mut source = UnattributedLogSinkSource::new();
        let mut publisher = source.get_publisher();
        let (_, log_sink_stream) =
            fidl::endpoints::create_proxy_and_stream::<LogSinkMarker>().unwrap();
        publisher.send(log_sink_stream).await.expect("send stream");

        let (event_sender, receiver) = mpsc::channel(1);
        source.listen(event_sender).await.expect("listen succeeds");

        let mut stream = Box::pin(receiver.boxed());
        let event = stream.next().await.expect("received event");
        let _expected_identity = ComponentIdentity {
            url: "fuchsia-pkg://UNKNOWN".to_string(),
            relative_moniker: vec!["UNKNOWN".to_string()],
            rendered_moniker: "UNKNOWN:0".to_string(),
            unique_key: vec!["UNKNOWN".to_string(), "0".to_string()],
        };
        assert_matches!(
            event,
            ComponentEvent::LogSinkRequested(LogSinkRequestedEvent {
                metadata: EventMetadata { identity: _expected_identity, .. },
                ..
            })
        );
    }
}
