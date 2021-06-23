// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::TEST_ROOT_REALM_NAME,
    anyhow::Error,
    async_trait::async_trait,
    diagnostics_bridge::ArchiveReaderManager,
    diagnostics_data::{Data, LogsData},
    diagnostics_reader as reader,
    fidl::endpoints::ServerEnd,
    fidl_fuchsia_developer_remotecontrol::StreamError,
    fidl_fuchsia_diagnostics::{
        ArchiveAccessorProxy, ArchiveAccessorRequest, ArchiveAccessorRequestStream,
        BatchIteratorMarker, BatchIteratorProxy, BatchIteratorRequest, ClientSelectorConfiguration,
        ComponentSelector, DataType, Format, FormattedContent, Selector, SelectorArgument,
        StreamMode, StreamParameters, StringSelector,
    },
    fidl_fuchsia_mem as fmem, fuchsia_async as fasync, fuchsia_zircon as zx,
    futures::{stream::FusedStream, TryStreamExt},
    serde_json::{self, Value as JsonValue},
    std::{ops::Deref, sync::Arc, sync::Weak},
    tracing::{error, warn},
};

pub struct IsolatedLogsProvider {
    accessor: Arc<ArchiveAccessorProxy>,
}

impl IsolatedLogsProvider {
    pub fn new(accessor: Arc<ArchiveAccessorProxy>) -> Self {
        Self { accessor }
    }

    pub fn start_streaming_logs(
        &self,
        iterator: ServerEnd<BatchIteratorMarker>,
    ) -> Result<(), StreamError> {
        let stream_parameters = StreamParameters {
            stream_mode: Some(StreamMode::SnapshotThenSubscribe),
            data_type: Some(DataType::Logs),
            format: Some(Format::Json),
            client_selector_configuration: Some(ClientSelectorConfiguration::SelectAll(true)),
            ..StreamParameters::EMPTY
        };
        self.accessor.stream_diagnostics(stream_parameters, iterator).map_err(|err| {
            warn!(%err, "Failed to subscribe to isolated logs");
            StreamError::SetupSubscriptionFailed
        })?;
        Ok(())
    }
}

impl Deref for IsolatedLogsProvider {
    type Target = Arc<ArchiveAccessorProxy>;

    fn deref(&self) -> &Self::Target {
        &self.accessor
    }
}

#[async_trait]
impl ArchiveReaderManager for IsolatedLogsProvider {
    type Error = reader::Error;

    async fn snapshot<D: diagnostics_data::DiagnosticsData + 'static>(
        &self,
    ) -> Result<Vec<Data<D>>, StreamError> {
        unimplemented!("This functionality is not yet needed.");
    }

    fn start_log_stream(
        &mut self,
    ) -> Result<
        Box<dyn FusedStream<Item = Result<LogsData, Self::Error>> + Unpin + Send>,
        StreamError,
    > {
        let (proxy, batch_iterator_server) = fidl::endpoints::create_proxy::<BatchIteratorMarker>()
            .map_err(|err| {
                warn!(%err, "Fidl error while creating proxy");
                StreamError::GenericError
            })?;
        self.start_streaming_logs(batch_iterator_server)?;
        let subscription = reader::Subscription::new(proxy);
        Ok(Box::new(subscription))
    }
}

/// Runs an ArchiveAccessor to which test components connect.
/// This will append the test realm name to all selectors coming from the component.
pub async fn run_intermediary_archive_accessor(
    embedded_archive_accessor: Weak<ArchiveAccessorProxy>,
    mut stream: ArchiveAccessorRequestStream,
) -> Result<(), Error> {
    while let Some(ArchiveAccessorRequest::StreamDiagnostics {
        result_stream,
        stream_parameters,
        control_handle: _,
    }) = stream.try_next().await?
    {
        let embedded_archive_accessor = match embedded_archive_accessor.upgrade() {
            Some(e) => e,
            None => break,
        };
        let (iterator, server_end) = fidl::endpoints::create_proxy::<BatchIteratorMarker>()?;
        let stream_parameters = scope_stream_parameters(stream_parameters);
        embedded_archive_accessor.stream_diagnostics(stream_parameters, server_end)?;

        fasync::Task::spawn(async move {
            interpose_batch_iterator_responses(iterator, result_stream).await.unwrap_or_else(|e| {
                error!("Failed running batch iterator: {:?}", e);
            })
        })
        .detach();
    }
    Ok(())
}

/// Forward BatchIterator#GetNext requests to the actual archivist and remove the `test_root`
/// prefixes from the monikers in the response.
async fn interpose_batch_iterator_responses(
    iterator: BatchIteratorProxy,
    client_server_end: ServerEnd<BatchIteratorMarker>,
) -> Result<(), Error> {
    let mut request_stream = client_server_end.into_stream()?;
    while let Some(BatchIteratorRequest::GetNext { responder }) = request_stream.try_next().await? {
        let result = iterator.get_next().await?;
        match result {
            Err(e) => responder.send(&mut Err(e))?,
            Ok(batch) => {
                let batch = batch
                    .into_iter()
                    .map(|f| scope_formatted_content(f))
                    .collect::<Result<Vec<_>, _>>()?;
                responder.send(&mut Ok(batch))?;
            }
        }
    }

    Ok(())
}

fn scope_formatted_content(content: FormattedContent) -> Result<FormattedContent, Error> {
    match content {
        FormattedContent::Json(data) => {
            let json_value = load_json_value(data)?;
            let value = match json_value {
                value @ JsonValue::Object(_) => scope_formatted_content_json(value),
                JsonValue::Array(objects) => {
                    let objects = objects
                        .into_iter()
                        .map(|object| scope_formatted_content_json(object))
                        .collect::<Vec<_>>();
                    JsonValue::Array(objects)
                }
                _ => unreachable!("ArchiveAccessor only returns top-level objects and arrays"),
            };
            let buffer = write_json_value(value)?;
            Ok(FormattedContent::Json(buffer))
        }
        // This should never be reached as the Archivist is not serving Text at the moment. When it
        // does we can decide how to parse it to scope this, but for now, not scoping.
        data @ FormattedContent::Text(_) => Ok(data),
        other => Ok(other),
    }
}

fn scope_formatted_content_json(mut object: JsonValue) -> JsonValue {
    object.get_mut("moniker").map(|moniker| match moniker {
        JsonValue::String(ref mut moniker) => {
            if let Some(updated) = moniker.strip_prefix(&format!("{}/", TEST_ROOT_REALM_NAME)) {
                *moniker = updated.to_string();
            }
        }
        _ => unreachable!("ArchiveAccessor always returns a moniker in the payload"),
    });
    object
}

fn load_json_value(data: fmem::Buffer) -> Result<JsonValue, Error> {
    let mut buf = vec![0; data.size as usize];
    data.vmo.read(&mut buf, 0)?;
    let hierarchy_json = std::str::from_utf8(&buf)?;
    let result = serde_json::from_str(&hierarchy_json)?;
    Ok(result)
}

fn write_json_value(value: JsonValue) -> Result<fmem::Buffer, Error> {
    let content = value.to_string();
    let size = content.len() as u64;
    let vmo = zx::Vmo::create(size)?;
    vmo.write(content.as_bytes(), 0)?;
    Ok(fmem::Buffer { vmo, size })
}

fn scope_stream_parameters(stream_parameters: StreamParameters) -> StreamParameters {
    StreamParameters {
        client_selector_configuration: stream_parameters.client_selector_configuration.map(
            |config| match config {
                ClientSelectorConfiguration::Selectors(selectors) => {
                    ClientSelectorConfiguration::Selectors(
                        selectors
                            .into_iter()
                            .map(|selector_argument| scope_selector_argument(selector_argument))
                            .collect::<Vec<_>>(),
                    )
                }
                other => other,
            },
        ),
        ..stream_parameters
    }
}

fn scope_selector_argument(selector_argument: SelectorArgument) -> SelectorArgument {
    match selector_argument {
        SelectorArgument::StructuredSelector(selector) => {
            SelectorArgument::StructuredSelector(Selector {
                tree_selector: selector.tree_selector,
                component_selector: selector.component_selector.map(|component_selector| {
                    ComponentSelector {
                        moniker_segments: component_selector.moniker_segments.map(
                            |mut segments| {
                                let mut moniker_segments = vec![StringSelector::ExactMatch(
                                    TEST_ROOT_REALM_NAME.to_string(),
                                )];
                                moniker_segments.append(&mut segments);
                                moniker_segments
                            },
                        ),
                        ..component_selector
                    }
                }),
                ..selector
            })
        }
        SelectorArgument::RawSelector(selector) => {
            SelectorArgument::RawSelector(format!("{}/{}", TEST_ROOT_REALM_NAME, selector))
        }
        other => other,
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        diagnostics_data::Data,
        diagnostics_hierarchy::hierarchy,
        fidl_fuchsia_diagnostics::ArchiveAccessorMarker,
        futures::{channel::mpsc, SinkExt, StreamExt},
    };

    #[fasync::run_singlethreaded(test)]
    async fn verify_archive_accessor_server_scopes_monikers() {
        let (sender, receiver) = mpsc::channel(1);
        let embedded_accessor = spawn_fake_archive_accessor(sender);
        let (test_accessor, stream) =
            fidl::endpoints::create_proxy_and_stream::<ArchiveAccessorMarker>()
                .expect("create our archive accessor proxy");
        let accessor = Arc::new(embedded_accessor);
        let accessor_clone = accessor.clone();
        fasync::Task::spawn(async move {
            run_intermediary_archive_accessor(Arc::downgrade(&accessor_clone), stream)
                .await
                .expect("ran proxyed archive accessor");
        })
        .detach();

        let (iterator, server_end) = fidl::endpoints::create_proxy::<BatchIteratorMarker>()
            .expect("create batch iterator proxy");

        let stream_parameters = StreamParameters {
            client_selector_configuration: Some(ClientSelectorConfiguration::Selectors(vec![
                SelectorArgument::RawSelector("foo/bar/component".to_string()),
                SelectorArgument::StructuredSelector(Selector {
                    component_selector: Some(ComponentSelector {
                        moniker_segments: Some(vec![StringSelector::StringPattern(
                            "foo".to_string(),
                        )]),
                        ..ComponentSelector::EMPTY
                    }),
                    ..Selector::EMPTY
                }),
            ])),
            ..StreamParameters::EMPTY
        };
        test_accessor
            .stream_diagnostics(stream_parameters, server_end)
            .expect("stream diagnostics ok");

        // Verify that the selectors received by the embedded archivist are scoped to the test root.
        let mut params_stream = receiver.boxed();
        let params = params_stream.next().await.expect("got params");
        assert_eq!(
            params,
            StreamParameters {
                client_selector_configuration: Some(ClientSelectorConfiguration::Selectors(vec![
                    SelectorArgument::RawSelector("test_root/foo/bar/component".to_string()),
                    SelectorArgument::StructuredSelector(Selector {
                        component_selector: Some(ComponentSelector {
                            moniker_segments: Some(vec![
                                StringSelector::ExactMatch("test_root".to_string()),
                                StringSelector::StringPattern("foo".to_string()),
                            ]),
                            ..ComponentSelector::EMPTY
                        }),
                        ..Selector::EMPTY
                    }),
                ])),
                ..StreamParameters::EMPTY
            }
        );

        // Verify that none of the monikers received from the batch contain the `test_root` prefix.
        let batch = iterator.get_next().await.expect("got batch").expect("batch is not an error");
        let batch = batch
            .into_iter()
            .map(|content| match content {
                FormattedContent::Json(data) => {
                    let json_value = load_json_value(data).expect("got json value");
                    json_value.get("moniker").unwrap().as_str().unwrap().to_string()
                }
                _ => unreachable!("our fake accessor just sends json"),
            })
            .collect::<Vec<_>>();

        assert_eq!(
            batch,
            vec!["foo/bar/component".to_string(), "baz/qux/other_component".to_string(),]
        );
    }

    fn spawn_fake_archive_accessor(
        mut seen_stream_parameters: mpsc::Sender<StreamParameters>,
    ) -> ArchiveAccessorProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<ArchiveAccessorMarker>()
                .expect("create proxy");
        fasync::Task::spawn(async move {
            while let Some(ArchiveAccessorRequest::StreamDiagnostics {
                stream_parameters,
                result_stream,
                ..
            }) = stream.try_next().await.expect("stream request")
            {
                seen_stream_parameters.send(stream_parameters).await.expect("send seen parameters");
                fasync::Task::spawn(async move {
                    let mut stream = result_stream.into_stream().expect("into stream");
                    while let Some(BatchIteratorRequest::GetNext { responder }) =
                        stream.try_next().await.expect("stream request")
                    {
                        let results = vec![
                            make_result("test_root/foo/bar/component"),
                            make_result("baz/qux/other_component"),
                        ];
                        responder.send(&mut Ok(results)).expect("send response");
                    }
                })
                .detach();
            }
        })
        .detach();

        proxy
    }

    fn make_result(moniker: &str) -> FormattedContent {
        let result = Data::for_inspect(
            moniker,
            Some(hierarchy! {
                root: {
                    x: 1u64,
                }
            }),
            0,
            "http://component",
            "fuchsia.inspect.Tree",
            vec![],
        );
        let json_value = serde_json::to_value(result).expect("data to json");
        let buffer = write_json_value(json_value).expect("json value to vmo buffer");
        FormattedContent::Json(buffer)
    }
}
