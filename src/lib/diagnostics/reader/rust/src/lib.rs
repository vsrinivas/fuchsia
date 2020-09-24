// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// TODO(fxbug.dev/58038) use thiserror for library errors
use anyhow::{Context as _, Error};
use diagnostics_data::{Data, DiagnosticsData, InspectData};
use fidl;
use fidl_fuchsia_diagnostics::{
    ArchiveAccessorMarker, ArchiveAccessorProxy, BatchIteratorMarker, BatchIteratorProxy,
    ClientSelectorConfiguration, Format, FormattedContent, SelectorArgument, StreamMode,
    StreamParameters,
};
use fuchsia_async::{self as fasync, DurationExt, TimeoutExt};
use fuchsia_component::client;
use fuchsia_zircon::{Duration, DurationNum};
use futures::sink::{Sink, SinkExt};
use serde_json::Value as JsonValue;

pub use diagnostics_data::{Inspect, Lifecycle, Logs};
pub use fidl_fuchsia_diagnostics::DataType;
pub use fuchsia_inspect_node_hierarchy::{NodeHierarchy, Property};

const RETRY_DELAY_MS: i64 = 300;

/// An inspect tree selector for a component.
pub struct ComponentSelector {
    relative_moniker: Vec<String>,
    tree_selectors: Vec<String>,
}

impl ComponentSelector {
    /// Create a new component event selector.
    /// By default it will select the whole tree unless tree selectors are provided.
    /// `relative_moniker` is the realm path relative to the realm of the running component plus the
    /// component name. For example: [a, b, component.cmx].
    pub fn new(relative_moniker: Vec<String>) -> Self {
        Self { relative_moniker, tree_selectors: Vec::new() }
    }

    /// Select a section of the inspect tree.
    pub fn with_tree_selector(mut self, tree_selector: impl Into<String>) -> Self {
        self.tree_selectors.push(tree_selector.into());
        self
    }

    fn relative_moniker_str(&self) -> String {
        self.relative_moniker.join("/")
    }
}

pub trait ToSelectorArguments {
    fn to_selector_arguments(self) -> Vec<String>;
}

impl ToSelectorArguments for String {
    fn to_selector_arguments(self) -> Vec<String> {
        vec![self]
    }
}

impl ToSelectorArguments for &str {
    fn to_selector_arguments(self) -> Vec<String> {
        vec![self.to_string()]
    }
}

impl ToSelectorArguments for ComponentSelector {
    fn to_selector_arguments(self) -> Vec<String> {
        let relative_moniker = self.relative_moniker_str();
        // If not tree selectors were provided, select the full tree.
        if self.tree_selectors.is_empty() {
            vec![format!("{}:root", relative_moniker.clone())]
        } else {
            self.tree_selectors
                .iter()
                .map(|s| format!("{}:{}", relative_moniker.clone(), s.clone()))
                .collect()
        }
    }
}

/// Utility for reading inspect data of a running component using the injected Archive
/// Reader service.
#[derive(Clone)]
pub struct ArchiveReader {
    archive: Option<ArchiveAccessorProxy>,
    selectors: Vec<String>,
    should_retry: bool,
    minimum_schema_count: usize,
    timeout: Option<Duration>,
    batch_retrieval_timeout_seconds: Option<i64>,
}

impl ArchiveReader {
    /// Creates a new data fetcher with default configuration:
    ///  - Maximum retries: 2^64-1
    ///  - Timeout: Never. Use with_timeout() to set a timeout.
    pub fn new() -> Self {
        Self {
            timeout: None,
            selectors: vec![],
            should_retry: true,
            archive: None,
            minimum_schema_count: 1,
            batch_retrieval_timeout_seconds: None,
        }
    }

    pub fn with_archive(mut self, archive: ArchiveAccessorProxy) -> Self {
        self.archive = Some(archive);
        self
    }

    /// Requests a single component tree (or sub-tree).
    pub fn add_selector(mut self, selector: impl ToSelectorArguments) -> Self {
        self.selectors.extend(selector.to_selector_arguments().into_iter());
        self
    }

    /// Requests to retry when an empty result is received.
    pub fn retry_if_empty(mut self, retry: bool) -> Self {
        self.should_retry = retry;
        self
    }

    pub fn add_selectors<T, S>(self, selectors: T) -> Self
    where
        T: Iterator<Item = S>,
        S: ToSelectorArguments,
    {
        let mut this = self;
        for selector in selectors {
            this = this.add_selector(selector);
        }
        this
    }

    /// Sets the maximum time to wait for a response from the Archive.
    /// Do not use in tests unless timeout is the expected behavior.
    pub fn with_timeout(mut self, duration: Duration) -> Self {
        self.timeout = Some(duration);
        self
    }

    /// Set the maximum time to wait for a wait for a single component
    /// to have its diagnostics data "pumped".
    pub fn with_batch_retrieval_timeout_seconds(mut self, timeout: i64) -> Self {
        self.batch_retrieval_timeout_seconds = Some(timeout);
        self
    }

    /// Sets the minumum number of schemas expected in a result in order for the
    /// result to be considered a success.
    pub fn with_minimum_schema_count(mut self, minimum_schema_count: usize) -> Self {
        self.minimum_schema_count = minimum_schema_count;
        self
    }

    /// Connects to the ArchiveAccessor and returns data matching provided selectors.
    pub async fn snapshot<D>(&self) -> Result<Vec<Data<D>>, Error>
    where
        D: DiagnosticsData,
    {
        let raw_json = self.snapshot_raw(D::DATA_TYPE).await?;
        Ok(serde_json::from_value(raw_json)?)
    }

    /// Use `snapshot::<Inspect>()` instead for identical functionality.
    pub async fn get(self) -> Result<Vec<InspectData>, Error> {
        // TODO delete after internal CL 238572 lands
        self.snapshot::<Inspect>().await
    }

    /// Connects to the ArchiveAccessor and returns inspect data matching provided selectors.
    /// Returns the raw json for each hierarchy fetched.
    pub async fn snapshot_raw(&self, ty: DataType) -> Result<JsonValue, Error> {
        let timeout = self.timeout;
        let data_future = self.snapshot_raw_inner(ty);
        let data = match timeout {
            Some(timeout) => data_future.on_timeout(timeout.after_now(), || Ok(Vec::new())).await?,
            None => data_future.await?,
        };
        Ok(JsonValue::Array(data))
    }

    async fn snapshot_raw_inner(&self, data_type: DataType) -> Result<Vec<JsonValue>, Error> {
        loop {
            let mut result = Vec::new();
            let iterator = self.batch_iterator(data_type, StreamMode::Snapshot)?;
            drain_batch_iterator(iterator, &mut result).await?;

            if result.len() < self.minimum_schema_count && self.should_retry {
                fasync::Timer::new(fasync::Time::after(RETRY_DELAY_MS.millis())).await;
            } else {
                return Ok(result);
            }
        }
    }

    fn batch_iterator(
        &self,
        data_type: DataType,
        mode: StreamMode,
    ) -> Result<BatchIteratorProxy, Error> {
        let archive = if let Some(archive) = &self.archive {
            archive.clone()
        } else {
            // TODO(fxbug.dev/58051) this should be done in an ArchiveReaderBuilder -> Reader init
            client::connect_to_service::<ArchiveAccessorMarker>().context("connect to archive")?
        };

        let (iterator, server_end) = fidl::endpoints::create_proxy::<BatchIteratorMarker>()
            .context("failed to create iterator proxy")?;

        let mut stream_parameters = StreamParameters::empty();
        stream_parameters.stream_mode = Some(mode);
        stream_parameters.data_type = Some(data_type);
        stream_parameters.format = Some(Format::Json);
        stream_parameters.batch_retrieval_timeout_seconds = self.batch_retrieval_timeout_seconds;
        stream_parameters.client_selector_configuration = if self.selectors.is_empty() {
            Some(ClientSelectorConfiguration::SelectAll(true))
        } else {
            Some(ClientSelectorConfiguration::Selectors(
                self.selectors
                    .iter()
                    .map(|selector| SelectorArgument::RawSelector(selector.clone()))
                    .collect(),
            ))
        };

        archive.stream_diagnostics(stream_parameters, server_end).context("get BatchIterator")?;
        Ok(iterator)
    }
}

async fn drain_batch_iterator<S, E>(
    iterator: BatchIteratorProxy,
    mut results: S,
) -> Result<(), Error>
where
    S: Sink<JsonValue, Error = E> + Unpin,
    E: std::error::Error + Send + Sync + 'static,
{
    loop {
        let next_batch = iterator.get_next().await.context("getting batch")?.unwrap();
        if next_batch.is_empty() {
            return Ok(());
        }
        for formatted_content in next_batch {
            match formatted_content {
                FormattedContent::Json(data) => {
                    let mut buf = vec![0; data.size as usize];
                    data.vmo.read(&mut buf, 0).context("reading vmo")?;
                    let hierarchy_json = std::str::from_utf8(&buf).unwrap();
                    let output: JsonValue =
                        serde_json::from_str(&hierarchy_json).context("valid json")?;

                    match output {
                        output @ JsonValue::Object(_) => {
                            results.send(output).await.context("sending result")?;
                        }
                        JsonValue::Array(values) => {
                            for value in values {
                                results.send(value).await.context("sending result")?;
                            }
                        }
                        _ => unreachable!(
                            "ArchiveAccessor only returns top-level objects and arrays"
                        ),
                    }
                }
                _ => unreachable!("JSON was requested, no other data type should be received"),
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::format_err,
        diagnostics_data::{Data, LifecycleType},
        fidl_fuchsia_diagnostics as fdiagnostics,
        fidl_fuchsia_sys::ComponentControllerEvent,
        fuchsia_component::{
            client::App,
            server::{NestedEnvironment, ServiceFs},
        },
        fuchsia_inspect::assert_inspect_tree,
        fuchsia_zircon as zx,
        futures::{StreamExt, TryStreamExt},
    };

    const TEST_COMPONENT_URL: &str =
        "fuchsia-pkg://fuchsia.com/diagnostics-reader-tests#meta/inspect_test_component.cmx";

    async fn start_component(env_label: &str) -> Result<(NestedEnvironment, App), Error> {
        let mut service_fs = ServiceFs::new();
        let env = service_fs.create_nested_environment(env_label)?;
        let app = client::launch(&env.launcher(), TEST_COMPONENT_URL.to_string(), None)?;
        fasync::Task::spawn(service_fs.collect()).detach();
        let mut component_stream = app.controller().take_event_stream();
        match component_stream
            .next()
            .await
            .expect("component event stream ended before termination event")?
        {
            ComponentControllerEvent::OnTerminated { return_code, termination_reason } => {
                return Err(format_err!(
                    "Component terminated unexpectedly. Code: {}. Reason: {:?}",
                    return_code,
                    termination_reason
                ));
            }
            ComponentControllerEvent::OnDirectoryReady {} => {}
        }
        Ok((env, app))
    }

    #[fasync::run_singlethreaded(test)]
    async fn lifecycle_events_for_component() {
        let (_env, _app) = start_component("test-lifecycle").await.unwrap();

        let results = ArchiveReader::new()
            .snapshot::<Lifecycle>()
            .await
            .unwrap()
            .into_iter()
            // TODO(fxbug.dev/51165) use selectors for this filtering
            .filter(|e| e.moniker.starts_with("test-lifecycle"))
            .collect::<Vec<_>>();
        assert!(results.len() >= 1, "should have at least a started event");

        let started = &results[0];
        assert_eq!(started.metadata.lifecycle_event_type, LifecycleType::Started);
        assert_eq!(started.metadata.component_url, TEST_COMPONENT_URL);
        assert_eq!(started.moniker, "test-lifecycle/inspect_test_component.cmx");
        assert_eq!(started.payload, None);
    }

    #[fasync::run_singlethreaded(test)]
    async fn inspect_data_for_component() -> Result<(), Error> {
        let (_env, _app) = start_component("test-ok").await?;

        let results = ArchiveReader::new()
            .add_selector("test-ok/inspect_test_component.cmx:root".to_string())
            .snapshot::<Inspect>()
            .await?;

        assert_eq!(results.len(), 1);
        assert_inspect_tree!(results[0].payload.as_ref().unwrap(), root: {
            int: 3u64,
            "lazy-node": {
                a: "test",
                child: {
                    double: 3.14,
                },
            }
        });

        let response = ArchiveReader::new()
            .add_selector(
                ComponentSelector::new(vec![
                    "test-ok".to_string(),
                    "inspect_test_component.cmx".to_string(),
                ])
                .with_tree_selector("root:int")
                .with_tree_selector("root/lazy-node:a"),
            )
            .snapshot::<Inspect>()
            .await?;

        assert_eq!(response.len(), 1);

        assert_inspect_tree!(response[0].payload.as_ref().unwrap(), root: {
            int: 3u64,
            "lazy-node": {
                a: "test"
            }
        });

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn timeout() -> Result<(), Error> {
        let (_env, _app) = start_component("test-timeout").await?;

        let result = ArchiveReader::new()
            .add_selector("test-timeout/inspect_test_component.cmx:root")
            .with_timeout(0.nanos())
            .snapshot::<Inspect>()
            .await;
        assert!(result.unwrap().is_empty());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn component_selector() {
        let selector = ComponentSelector::new(vec!["a.cmx".to_string()]);
        assert_eq!(selector.relative_moniker_str(), "a.cmx");
        let arguments: Vec<String> = selector.to_selector_arguments();
        assert_eq!(arguments, vec!["a.cmx:root".to_string()]);

        let selector =
            ComponentSelector::new(vec!["b".to_string(), "c".to_string(), "a.cmx".to_string()]);
        assert_eq!(selector.relative_moniker_str(), "b/c/a.cmx");

        let selector = selector.with_tree_selector("root/b/c:d").with_tree_selector("root/e:f");
        let arguments: Vec<String> = selector.to_selector_arguments();
        assert_eq!(
            arguments,
            vec!["b/c/a.cmx:root/b/c:d".to_string(), "b/c/a.cmx:root/e:f".to_string(),]
        );
    }

    #[fasync::run_singlethreaded(test)]
    async fn custom_archive() {
        let proxy = spawn_fake_archive();
        let result = ArchiveReader::new()
            .with_archive(proxy)
            .snapshot::<Inspect>()
            .await
            .expect("got result");
        assert_eq!(result.len(), 1);
        assert_inspect_tree!(result[0].payload.as_ref().unwrap(), root: { x: 1u64 });
    }

    fn spawn_fake_archive() -> fdiagnostics::ArchiveAccessorProxy {
        let (proxy, mut stream) =
            fidl::endpoints::create_proxy_and_stream::<fdiagnostics::ArchiveAccessorMarker>()
                .expect("create proxy");
        fasync::Task::spawn(async move {
            while let Some(request) = stream.try_next().await.expect("stream request") {
                match request {
                    fdiagnostics::ArchiveAccessorRequest::StreamDiagnostics {
                        result_stream,
                        ..
                    } => {
                        fasync::Task::spawn(async move {
                            let mut called = false;
                            let mut stream = result_stream.into_stream().expect("into stream");
                            while let Some(req) = stream.try_next().await.expect("stream request") {
                                match req {
                                    fdiagnostics::BatchIteratorRequest::GetNext { responder } => {
                                        if called {
                                            responder
                                                .send(&mut Ok(Vec::new()))
                                                .expect("send response");
                                            continue;
                                        }
                                        called = true;
                                        let result = Data::for_inspect(
                                            "moniker",
                                            Some(NodeHierarchy::new(
                                                "root",
                                                vec![Property::Uint("x".to_string(), 1)],
                                                vec![],
                                            )),
                                            0u64,
                                            "component-url",
                                            "filename",
                                            vec![],
                                        );
                                        let content = serde_json::to_string_pretty(&result)
                                            .expect("json pretty");
                                        let vmo_size = content.len() as u64;
                                        let vmo =
                                            zx::Vmo::create(vmo_size as u64).expect("create vmo");
                                        vmo.write(content.as_bytes(), 0).expect("write vmo");
                                        let buffer =
                                            fidl_fuchsia_mem::Buffer { vmo, size: vmo_size };
                                        responder
                                            .send(&mut Ok(vec![
                                                fdiagnostics::FormattedContent::Json(buffer),
                                            ]))
                                            .expect("send response");
                                    }
                                }
                            }
                        })
                        .detach();
                    }
                }
            }
        })
        .detach();
        return proxy;
    }
}
