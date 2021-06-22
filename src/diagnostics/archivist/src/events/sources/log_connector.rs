// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        container::ComponentIdentity,
        events::{error::EventError, types::*},
    },
    async_trait::async_trait,
    fidl_fuchsia_sys_internal::{
        LogConnection, LogConnectionListenerMarker, LogConnectionListenerRequest, LogConnectorProxy,
    },
    fuchsia_async as fasync,
    futures::{channel::mpsc, SinkExt, TryStreamExt},
    std::convert::TryFrom,
    tracing::{error, warn},
};

pub struct LogConnectorEventSource {
    connector: Option<LogConnectorProxy>,
    _log_connector_task: Option<fasync::Task<()>>,
}

impl LogConnectorEventSource {
    pub fn new(connector: LogConnectorProxy) -> Self {
        Self { connector: Some(connector), _log_connector_task: None }
    }
}

#[async_trait]
impl EventSource for LogConnectorEventSource {
    async fn listen(&mut self, sender: mpsc::Sender<ComponentEvent>) -> Result<(), EventError> {
        match self.connector.take() {
            None => Err(EventError::StreamAlreadyTaken),
            Some(connector) => {
                self._log_connector_task = Some(fasync::Task::spawn(async move {
                    match connector.take_log_connection_listener().await {
                        Ok(None) => {
                            warn!(
                                "local realm already gave out LogConnectionListener, skipping logs"
                            );
                        }
                        Err(err) => {
                            error!(%err, "Error occurred during LogConnectorEventSource initialization")
                        }
                        Ok(Some(connection)) => {
                            listen_for_log_connections(connection, sender).await
                        }
                    }
                }));
                Ok(())
            }
        }
    }
}
async fn listen_for_log_connections(
    listener: fidl::endpoints::ServerEnd<LogConnectionListenerMarker>,
    mut sender: mpsc::Sender<ComponentEvent>,
) {
    let mut connections = listener.into_stream().expect("getting request stream from server end");
    while let Ok(Some(connection)) = connections.try_next().await {
        match connection {
            LogConnectionListenerRequest::OnNewConnection {
                connection: LogConnection { log_request, source_identity },
                ..
            } => {
                let identity = match ComponentIdentity::try_from(source_identity) {
                    Ok(identity) => identity,
                    Err(err) => {
                        error!(%err, "Consuming SourceIdentity");
                        continue;
                    }
                };
                let requests =
                    log_request.into_stream().expect("getting request stream from server end");
                sender
                    .send(ComponentEvent::LogSinkRequested(LogSinkRequestedEvent {
                        metadata: EventMetadata::new(identity),
                        requests,
                    }))
                    .await
                    .expect("events channel should be always up");
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        fidl_fuchsia_logger::LogSinkMarker,
        fidl_fuchsia_sys_internal::{
            LogConnectionListenerProxy, LogConnectorMarker, LogConnectorRequest,
            LogConnectorRequestStream, SourceIdentity,
        },
        futures::StreamExt,
        matches::assert_matches,
    };

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
        let mut source = LogConnectorEventSource::new(connector);
        let (sender, receiver) = mpsc::channel(1);
        source.listen(sender).await.expect("listen succeeds");

        let (_, log_request) =
            fidl::endpoints::create_proxy::<LogSinkMarker>().expect("create proxy");
        let source_identity = SourceIdentity {
            realm_path: Some(vec![]),
            component_name: Some("testing123".to_string()),
            instance_id: Some("0".to_string()),
            component_url: Some("fuchsia-pkg://test".to_string()),
            ..SourceIdentity::EMPTY
        };
        log_connection_listener
            .on_new_connection(&mut LogConnection { log_request, source_identity })
            .expect("on new connection");
        let mut stream = Box::pin(receiver.boxed());
        let event = stream.next().await.expect("received event");
        let _expected_identity = ComponentIdentity {
            url: "fuchsia-pkg://test".to_string(),
            relative_moniker: vec!["testing"].into(),
            rendered_moniker: "testing:0".to_string(),
            unique_key: vec!["testing", "0"].into(),
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
