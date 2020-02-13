// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    anyhow::{format_err, Context, Error},
    fidl_fuchsia_diagnostics::{
        ArchiveMarker, BatchIteratorMarker, Format, FormattedContent, ReaderMarker,
        SelectorArgument,
    },
    fuchsia_async::{self as fasync, DurationExt, TimeoutExt},
    fuchsia_component::client,
    fuchsia_inspect_node_hierarchy::{
        serialization::{HierarchyDeserializer, RawJsonNodeHierarchySerializer},
        NodeHierarchy,
    },
    fuchsia_zircon::{Duration, DurationNum},
    lazy_static::lazy_static,
    serde_json,
};

lazy_static! {
    static ref RETRY_DELAY_MS: i64 = 150;
    static ref DEFAULT_TIMEOUT_SECS: i64 = 5;
    static ref MAX_RETRIES: usize = std::usize::MAX;
}

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

impl Into<Vec<SelectorArgument>> for ComponentSelector {
    fn into(self) -> Vec<SelectorArgument> {
        let relative_moniker = self.relative_moniker_str();
        // If not tree selectors were provided, select the full tree.
        if self.tree_selectors.is_empty() {
            vec![SelectorArgument::RawSelector(format!("{}:root", relative_moniker))]
        } else {
            self.tree_selectors
                .into_iter()
                .map(|s| SelectorArgument::RawSelector(format!("{}:{}", relative_moniker, s)))
                .collect()
        }
    }
}

/// Utility for reading inspect data of a running component using the injected observer.cmx Archive
/// Reader service.
pub struct InspectDataFetcher {
    selectors: Vec<ComponentSelector>,
    timeout: Duration,
}

impl InspectDataFetcher {
    /// Creates a new data fetcher with default configuration:
    ///  - Maximum retries: 2^64-1
    ///  - Timeout: 5 seconds
    pub fn new() -> Self {
        Self { timeout: DEFAULT_TIMEOUT_SECS.seconds(), selectors: vec![] }
    }

    /// Requests a single component tree (or sub-tree).
    pub fn add_selector(mut self, selector: ComponentSelector) -> Self {
        self.selectors.push(selector);
        self
    }

    /// Sets the maximum time to wait for a response from the Archive.
    pub fn with_timeout(mut self, duration: Duration) -> Self {
        self.timeout = duration;
        self
    }

    /// Connects to the archivist observer.cmx and returns inspect data associated with the given
    /// component under the relative realm path given.
    pub async fn get(self) -> Result<Vec<NodeHierarchy>, Error> {
        let timeout = self.timeout;
        self.get_inspect_data().on_timeout(timeout.after_now(), || Ok(Vec::new())).await
    }

    async fn get_inspect_data(self) -> Result<Vec<NodeHierarchy>, Error> {
        let archive =
            client::connect_to_service::<ArchiveMarker>().context("connect to archive")?;

        let (reader, server_end) =
            fidl::endpoints::create_proxy::<ReaderMarker>().context("connect to reader")?;

        let mut arguments = self
            .selectors
            .into_iter()
            .map(|s| {
                let arguments: Vec<SelectorArgument> = s.into();
                arguments.into_iter()
            })
            .flatten()
            .collect::<Vec<SelectorArgument>>();

        let read_fut = archive.read_inspect(server_end, &mut arguments.iter_mut());
        read_fut.await.context("get inspect reader")?.unwrap();

        let mut retry = 0;
        loop {
            if retry > *MAX_RETRIES {
                return Err(format_err!("Maximum retries"));
            }

            let (iterator, server_end) = fidl::endpoints::create_proxy::<BatchIteratorMarker>()
                .context("failed to create iterator proxy")?;
            reader
                .get_snapshot(Format::Json, server_end)
                .await
                .context("get BatchIterator")?
                .unwrap();

            let mut result = Vec::new();
            loop {
                let next_batch = iterator.get_next().await.context("failed to get batch")?.unwrap();
                if next_batch.is_empty() {
                    break;
                }
                for formatted_content in next_batch {
                    match formatted_content {
                        FormattedContent::FormattedJsonHierarchy(data) => {
                            let mut buf = vec![0; data.size as usize];
                            data.vmo.read(&mut buf, 0).context("reading vmo")?;
                            let hierarchy_json = std::str::from_utf8(&buf).unwrap();
                            let mut output: serde_json::Value =
                                serde_json::from_str(&hierarchy_json).context("valid json")?;
                            let tree_json =
                                output.get_mut("contents").context("contents are there")?.take();
                            result.push(RawJsonNodeHierarchySerializer::deserialize(tree_json)?);
                        }
                        _ => unreachable!(
                            "JSON was requested, no other data type should be received"
                        ),
                    }
                }
            }

            if result.is_empty() {
                // Retry with delay to ensure data appears if the reader is called right after the
                // component started, before the archivist knows about it.
                fasync::Timer::new(fasync::Time::after(RETRY_DELAY_MS.millis())).await;
            } else {
                return Ok(result);
            }

            retry += 1;
        }
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        anyhow::format_err,
        fidl_fuchsia_sys::ComponentControllerEvent,
        fuchsia_component::{
            client::App,
            server::{NestedEnvironment, ServiceFs},
        },
        futures::StreamExt,
    };

    const TEST_COMPONENT_URL: &str =
        "fuchsia-pkg://fuchsia.com/fuchsia_inspect_tests#meta/inspect_test_component.cmx";

    async fn start_component(env_label: &str) -> Result<(NestedEnvironment, App), Error> {
        let mut service_fs = ServiceFs::new();
        let env = service_fs.create_nested_environment(env_label)?;
        let app = client::launch(&env.launcher(), TEST_COMPONENT_URL.to_string(), None)?;
        fasync::spawn(service_fs.collect());
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
    async fn inspect_data_for_component() -> Result<(), Error> {
        let (_env, _app) = start_component("test-ok").await?;

        let hierarchies = InspectDataFetcher::new()
            .add_selector(ComponentSelector::new(vec![
                "test-ok".to_string(),
                "inspect_test_component.cmx".to_string(),
            ]))
            .get()
            .await?;

        assert_eq!(hierarchies.len(), 1);
        assert_inspect_tree!(hierarchies[0], root: {
            int: 3i64,
            "lazy-node": {
                a: "test",
                child: {
                    double: 3.14,
                },
            }
        });

        let hierarchies = InspectDataFetcher::new()
            .add_selector(
                ComponentSelector::new(vec![
                    "test-ok".to_string(),
                    "inspect_test_component.cmx".to_string(),
                ])
                .with_tree_selector("root:int")
                .with_tree_selector("root/lazy-node:a"),
            )
            .get()
            .await?;
        assert_eq!(hierarchies.len(), 1);
        assert_inspect_tree!(hierarchies[0], root: {
            int: 3i64,
            "lazy-node": {
                a: "test"
            }
        });

        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn timeout() -> Result<(), Error> {
        let (_env, _app) = start_component("test-timeout").await?;

        let result = InspectDataFetcher::new()
            .add_selector(ComponentSelector::new(vec![
                "test-timeout".to_string(),
                "inspect_test_component.cmx".to_string(),
            ]))
            .with_timeout(0.nanos())
            .get()
            .await;
        assert!(result.unwrap().is_empty());
        Ok(())
    }

    #[fasync::run_singlethreaded(test)]
    async fn component_selector() {
        let selector = ComponentSelector::new(vec!["a.cmx".to_string()]);
        assert_eq!(selector.relative_moniker_str(), "a.cmx");
        let arguments: Vec<SelectorArgument> = selector.into();
        assert_eq!(arguments, vec![SelectorArgument::RawSelector("a.cmx:root".to_string())]);

        let selector =
            ComponentSelector::new(vec!["b".to_string(), "c".to_string(), "a.cmx".to_string()]);
        assert_eq!(selector.relative_moniker_str(), "b/c/a.cmx");

        let selector = selector.with_tree_selector("root/b/c:d").with_tree_selector("root/e:f");
        let arguments: Vec<SelectorArgument> = selector.into();
        assert_eq!(
            arguments,
            vec![
                SelectorArgument::RawSelector("b/c/a.cmx:root/b/c:d".to_string()),
                SelectorArgument::RawSelector("b/c/a.cmx:root/e:f".to_string()),
            ]
        );
    }
}
