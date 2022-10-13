// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        events::{
            error::EventError,
            router::{Dispatcher, EventProducer},
            types::*,
        },
        identity::ComponentIdentity,
    },
    fidl_fuchsia_sys_internal::{LogConnection, LogConnectionListenerRequest, LogConnectorProxy},
    fuchsia_zircon as zx,
    futures::StreamExt,
    std::convert::TryFrom,
    tracing::{error, warn},
};

pub struct LogConnector {
    connector: LogConnectorProxy,
    dispatcher: Dispatcher,
}

impl LogConnector {
    pub fn new(connector: LogConnectorProxy) -> Self {
        Self { connector, dispatcher: Dispatcher::default() }
    }

    pub async fn spawn(mut self) -> Result<(), EventError> {
        let listener = match self.connector.take_log_connection_listener().await {
            Ok(None) => {
                warn!("local realm already gave out LogConnectionListener, skipping logs");
                return Ok(());
            }
            Err(err) => {
                return Err(err.into());
            }
            Ok(Some(listener)) => listener,
        };
        let mut connections =
            listener.into_stream().expect("getting request stream from server end");
        while let Some(Ok(request)) = connections.next().await {
            match request {
                LogConnectionListenerRequest::OnNewConnection {
                    connection: LogConnection { log_request, source_identity },
                    ..
                } => {
                    let component = match ComponentIdentity::try_from(source_identity) {
                        Ok(identity) => identity,
                        Err(err) => {
                            error!(%err, "Consuming SourceIdentity");
                            continue;
                        }
                    };
                    let requests =
                        log_request.into_stream().expect("getting request stream from server end");
                    if let Err(err) = self
                        .dispatcher
                        .emit(Event {
                            timestamp: zx::Time::get_monotonic(),
                            payload: EventPayload::LogSinkRequested(LogSinkRequestedPayload {
                                component,
                                request_stream: Some(requests),
                            }),
                        })
                        .await
                    {
                        if err.is_disconnected() {
                            break;
                        }
                    }
                }
            }
        }
        Ok(())
    }
}

impl EventProducer for LogConnector {
    fn set_dispatcher(&mut self, dispatcher: Dispatcher) {
        self.dispatcher = dispatcher;
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use fidl_fuchsia_logger::LogSinkMarker;
    use fidl_fuchsia_sys_internal::{
        LogConnectionListenerMarker, LogConnectionListenerProxy, LogConnectorMarker,
        LogConnectorRequest, LogConnectorRequestStream, SourceIdentity,
    };
    use fuchsia_async as fasync;
    use futures::StreamExt;
    use std::collections::BTreeSet;

    fn spawn_log_connector(mut stream: LogConnectorRequestStream) -> LogConnectionListenerProxy {
        let (client, server) =
            fidl::endpoints::create_proxy::<LogConnectionListenerMarker>().expect("create proxy");
        let mut server = Some(server);
        fasync::Task::spawn(async move {
            while let Some(Ok(LogConnectorRequest::TakeLogConnectionListener { responder })) =
                stream.next().await
            {
                responder.send(server.take()).expect("take log connection listener responds");
            }
        })
        .detach();
        client
    }

    #[fuchsia::test]
    async fn listen_for_events() {
        let (connector, request_stream) =
            fidl::endpoints::create_proxy_and_stream::<LogConnectorMarker>().expect("create proxy");
        let log_connection_listener = spawn_log_connector(request_stream);
        let mut log_connector = LogConnector::new(connector);
        let events = BTreeSet::from([EventType::LogSinkRequested]);
        let (mut event_stream, dispatcher) = Dispatcher::new_for_test(events);
        log_connector.set_dispatcher(dispatcher);
        let _task = fasync::Task::spawn(async move { log_connector.spawn().await });

        let (_, log_request) =
            fidl::endpoints::create_proxy::<LogSinkMarker>().expect("create proxy");
        let source_identity = SourceIdentity {
            realm_path: Some(vec![]),
            component_name: Some("testing".to_string()),
            instance_id: Some("0".to_string()),
            component_url: Some("fuchsia-pkg://test".to_string()),
            ..SourceIdentity::EMPTY
        };
        log_connection_listener
            .on_new_connection(&mut LogConnection { log_request, source_identity })
            .expect("on new connection");
        let event = event_stream.next().await.expect("received event");
        let expected_identity = ComponentIdentity {
            url: "fuchsia-pkg://test".to_string(),
            instance_id: Some("0".to_string()),
            relative_moniker: vec!["testing"].into(),
        };
        match event.payload {
            EventPayload::LogSinkRequested(LogSinkRequestedPayload {
                component,
                request_stream: Some(_),
            }) => {
                assert_eq!(component, expected_identity);
            }
            payload => unreachable!("should never get {:?}", payload),
        }
    }
}
