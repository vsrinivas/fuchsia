// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        data_repository::{
            DiagnosticsDataRepository, Moniker, PopulatedInspectDataContainer, ReadSnapshot,
            SnapshotData, UnpopulatedInspectDataContainer,
        },
        diagnostics,
        formatter::{self, Schema},
    },
    anyhow::{format_err, Error},
    fidl::endpoints::{RequestStream, ServerEnd},
    fidl_fuchsia_diagnostics::{self, BatchIteratorMarker, BatchIteratorRequestStream},
    fidl_fuchsia_mem, fuchsia_async as fasync,
    fuchsia_async::{DurationExt, TimeoutExt},
    fuchsia_inspect::{reader::PartialNodeHierarchy, NumericProperty},
    fuchsia_inspect_node_hierarchy::{InspectHierarchyMatcher, NodeHierarchy},
    fuchsia_zircon::{self as zx, DurationNum, HandleBased},
    futures::future::join_all,
    futures::stream::FusedStream,
    futures::{TryFutureExt, TryStreamExt},
    log::error,
    parking_lot::RwLock,
    selectors,
    std::collections::HashMap,
    std::convert::{TryFrom, TryInto},
    std::sync::Arc,
};

/// Keep only 64 hierarchy snapshots in memory at a time.
/// We limit to 64 because each snapshot is sent over a VMO and we can only have
/// 64 handles sent over a message.
// TODO(4601): Make this product-configurable.
// TODO(4601): Consider configuring batch sizes by bytes, not by hierarchy count.
static IN_MEMORY_SNAPSHOT_LIMIT: usize = 64;

// Number of seconds to wait for a single component to have its diagnostics data "pumped".
// This involves diagnostics directory traversal, contents extraction, and snapshotting.
pub static PER_COMPONENT_ASYNC_TIMEOUT_SECONDS: i64 = 10;

/// Packet containing a node hierarchy and all the metadata needed to
/// populate a diagnostics schema for that node hierarchy.
pub struct NodeHierarchyData {
    // Name of the file that created this snapshot.
    filename: String,
    // Timestamp at which this snapshot resolved or failed.
    timestamp: zx::Time,
    // Errors encountered when processing this snapshot.
    errors: Vec<formatter::Error>,
    // Optional NodeHierarchy of the inspect hierarchy, in case reading fails
    // and we have errors to share with client.
    hierarchy: Option<NodeHierarchy>,
}

impl Into<NodeHierarchyData> for SnapshotData {
    fn into(self: SnapshotData) -> NodeHierarchyData {
        match self.snapshot {
            Some(snapshot) => match convert_snapshot_to_node_hierarchy(snapshot) {
                Ok(node_hierarchy) => NodeHierarchyData {
                    filename: self.filename,
                    timestamp: self.timestamp,
                    errors: self.errors,
                    hierarchy: Some(node_hierarchy),
                },
                Err(e) => NodeHierarchyData {
                    filename: self.filename,
                    timestamp: self.timestamp,
                    errors: vec![formatter::Error { message: format!("{:?}", e) }],
                    hierarchy: None,
                },
            },
            None => NodeHierarchyData {
                filename: self.filename,
                timestamp: self.timestamp,
                errors: self.errors,
                hierarchy: None,
            },
        }
    }
}

/// ReaderServer holds the state and data needed to serve Inspect data
/// reading requests for a single client.
///
/// configured_selectors: are the selectors provided by the client which define
///                       what inspect data is returned by read requests. A none type
///                       implies that all available data should be returned.
///
/// inspect_repo: the DiagnosticsDataRepository which holds the access-points for all relevant
///               inspect data.
#[derive(Clone)]
pub struct ReaderServer {
    pub inspect_repo: Arc<RwLock<DiagnosticsDataRepository>>,
    pub configured_selectors: Option<Vec<Arc<fidl_fuchsia_diagnostics::Selector>>>,
    pub inspect_reader_server_stats: Arc<diagnostics::InspectReaderServerStats>,
}

fn convert_snapshot_to_node_hierarchy(snapshot: ReadSnapshot) -> Result<NodeHierarchy, Error> {
    match snapshot {
        ReadSnapshot::Single(snapshot) => Ok(PartialNodeHierarchy::try_from(snapshot)?.into()),
        ReadSnapshot::Tree(snapshot_tree) => snapshot_tree.try_into(),
        ReadSnapshot::Finished(hierarchy) => Ok(hierarchy),
    }
}

impl Drop for ReaderServer {
    fn drop(&mut self) {
        self.inspect_reader_server_stats.global_stats.inspect_reader_servers_destroyed.add(1);
    }
}

pub struct BatchResultItem {
    /// Relative moniker of the component associated with this result.
    pub moniker: Moniker,
    /// The url with which the component associated with this result was launched.
    pub component_url: String,
    /// The resulting Node hierarchy plus some metadata.
    pub hierarchy_data: NodeHierarchyData,
}

impl ReaderServer {
    pub fn new(
        inspect_repo: Arc<RwLock<DiagnosticsDataRepository>>,
        configured_selectors: Option<Vec<fidl_fuchsia_diagnostics::Selector>>,
        inspect_reader_server_stats: Arc<diagnostics::InspectReaderServerStats>,
    ) -> Self {
        inspect_reader_server_stats.global_stats.inspect_reader_servers_constructed.add(1);
        ReaderServer {
            inspect_repo,
            configured_selectors: configured_selectors.map(|selectors| {
                selectors.into_iter().map(|selector| Arc::new(selector)).collect()
            }),
            inspect_reader_server_stats,
        }
    }

    fn filter_single_components_snapshots(
        sanitized_moniker: String,
        snapshots: Vec<SnapshotData>,
        static_matcher: Option<InspectHierarchyMatcher>,
        client_matcher_container: &HashMap<String, Option<InspectHierarchyMatcher>>,
    ) -> Vec<NodeHierarchyData> {
        let statically_filtered_hierarchies: Vec<NodeHierarchyData> = match static_matcher {
            Some(static_matcher) => snapshots
                .into_iter()
                .map(|snapshot_data| {
                    let node_hierarchy_data: NodeHierarchyData = snapshot_data.into();

                    match node_hierarchy_data.hierarchy {
                        Some(node_hierarchy) => {
                            match fuchsia_inspect_node_hierarchy::filter_node_hierarchy(
                                node_hierarchy,
                                &static_matcher,
                            ) {
                                Ok(filtered_hierarchy_opt) => NodeHierarchyData {
                                    filename: node_hierarchy_data.filename,
                                    timestamp: node_hierarchy_data.timestamp,
                                    errors: node_hierarchy_data.errors,
                                    hierarchy: filtered_hierarchy_opt,
                                },
                                Err(e) => {
                                    error!("Archivist failed to filter a node hierarchy: {:?}", e);
                                    NodeHierarchyData {
                                        filename: node_hierarchy_data.filename,
                                        timestamp: node_hierarchy_data.timestamp,
                                        errors: vec![formatter::Error {
                                            message: format!("{:?}", e),
                                        }],
                                        hierarchy: None,
                                    }
                                }
                            }
                        }
                        None => NodeHierarchyData {
                            filename: node_hierarchy_data.filename,
                            timestamp: node_hierarchy_data.timestamp,
                            errors: node_hierarchy_data.errors,
                            hierarchy: None,
                        },
                    }
                })
                .collect(),

            // The only way we have a None value for the PopulatedDataContainer is
            // if there were no provided static selectors, which is only valid in
            // the AllAccess pipeline. For all other pipelines, if no static selectors
            // matched, the data wouldn't have ended up in the repository to begin
            // with.
            None => snapshots.into_iter().map(|snapshot_data| snapshot_data.into()).collect(),
        };

        match client_matcher_container.get(&sanitized_moniker) {
            // If the moniker key was present, and there was an InspectHierarchyMatcher,
            // then this means the client provided their own selectors, and a subset of
            // them matched this component. So we need to filter each of the snapshots from
            // this component with the dynamically provided components.
            Some(Some(dynamic_matcher)) => statically_filtered_hierarchies
                .into_iter()
                .map(|node_hierarchy_data| match node_hierarchy_data.hierarchy {
                    Some(node_hierarchy) => {
                        match fuchsia_inspect_node_hierarchy::filter_node_hierarchy(
                            node_hierarchy,
                            &dynamic_matcher,
                        ) {
                            Ok(filtered_hierarchy_opt) => NodeHierarchyData {
                                filename: node_hierarchy_data.filename,
                                timestamp: node_hierarchy_data.timestamp,
                                errors: node_hierarchy_data.errors,
                                hierarchy: filtered_hierarchy_opt,
                            },
                            Err(e) => NodeHierarchyData {
                                filename: node_hierarchy_data.filename,
                                timestamp: node_hierarchy_data.timestamp,
                                errors: vec![formatter::Error { message: format!("{:?}", e) }],
                                hierarchy: None,
                            },
                        }
                    }
                    None => NodeHierarchyData {
                        filename: node_hierarchy_data.filename,
                        timestamp: node_hierarchy_data.timestamp,
                        errors: node_hierarchy_data.errors,
                        hierarchy: None,
                    },
                })
                .collect(),
            // If the moniker key was present, but the InspectHierarchyMatcher option was
            // None, this means that the client provided their own selectors, and none of
            // them matched this particular component, so no values are to be returned.
            Some(None) => Vec::new(),
            // If the moniker key was absent, then the entire client_matcher_container should
            // be empty since the implication is that the client provided none of their own
            // selectors. Either every moniker is present or none are. And, if no dynamically
            // provided selectors exist, then the statically filtered snapshots are all that
            // we need.
            None => {
                assert!(client_matcher_container.is_empty());
                statically_filtered_hierarchies
            }
        }
    }

    /// Takes a batch of unpopulated inspect data containers, traverses their diagnostics
    /// directories, takes snapshots of all the Inspect hierarchies in those directories,
    /// and then transforms the data containers into `PopulatedInspectDataContainer` results.
    ///
    /// An entry is only an Error if connecting to the directory fails. Within a component's
    /// diagnostics directory, individual snapshots of hierarchies can fail and the transformation
    /// to a PopulatedInspectDataContainer will still succeed.
    async fn pump_inspect_data(
        &self,
        inspect_batch: Vec<UnpopulatedInspectDataContainer>,
    ) -> Vec<PopulatedInspectDataContainer> {
        join_all(inspect_batch.into_iter().map(move |inspect_data_packet| {
            let attempted_relative_moniker = inspect_data_packet.relative_moniker.clone();
            let attempted_inspect_matcher = inspect_data_packet.inspect_matcher.clone();
            let component_url = inspect_data_packet.component_url.clone();
            let global_stats = self.inspect_reader_server_stats.global_stats.clone();
            PopulatedInspectDataContainer::from(inspect_data_packet).on_timeout(
                PER_COMPONENT_ASYNC_TIMEOUT_SECONDS.seconds().after_now(),
                move || {
                    global_stats.component_timeouts_count.add(1);
                    let error_string = format!(
                        "Exceeded per-component time limit for fetching diagnostics data: {:?}",
                        attempted_relative_moniker
                    );
                    error!("{}", error_string);
                    let no_success_snapshot_data = vec![SnapshotData::failed(
                        formatter::Error { message: error_string },
                        "NO_FILE_SUCCEEDED".to_string(),
                    )];
                    PopulatedInspectDataContainer {
                        relative_moniker: attempted_relative_moniker,
                        component_url,
                        snapshots: no_success_snapshot_data,
                        inspect_matcher: attempted_inspect_matcher,
                    }
                },
            )
        }))
        .await
    }

    /// Takes a batch of PopulatedInspectDataContainer results, and for all the non-error
    /// entries converts all snapshots into in-memory node hierarchies, filters those hierarchies
    /// so that the only diagnostics properties they contain are those configured by the static
    /// and client-provided selectors, and then packages the filtered hierarchies into
    /// HierarchyData data structs.
    ///
    // TODO(4601): Error entries should still be included, but with a custom hierarchy
    //             that makes it clear to clients that snapshotting failed.
    pub fn filter_snapshots(
        &self,
        pumped_inspect_data: Vec<PopulatedInspectDataContainer>,
    ) -> Vec<BatchResultItem> {
        // In case we encounter multiple PopulatedDataContainers with the same moniker we don't
        // want to do the component selector filtering again, so store the results in a map.
        let mut client_selector_matches: HashMap<String, Option<InspectHierarchyMatcher>> =
            HashMap::new();

        // We iterate the vector of pumped inspect data packets, consuming each inspect vmo
        // and filtering it using the provided selector regular expressions. Each filtered
        // inspect hierarchy is then added to an accumulator as a HierarchyData to be converted
        // into a JSON string and returned.
        pumped_inspect_data.into_iter().fold(Vec::new(), |mut acc, pumped_data| {
            let sanitized_moniker = pumped_data
                .relative_moniker
                .iter()
                .map(|s| selectors::sanitize_string_for_selectors(s))
                .collect::<Vec<String>>()
                .join("/");

            if let Some(configured_selectors) = &self.configured_selectors {
                let configured_matchers =
                    client_selector_matches.entry(sanitized_moniker.clone()).or_insert_with(|| {
                        let matching_selectors =
                            selectors::match_component_moniker_against_selectors(
                                &pumped_data.relative_moniker,
                                configured_selectors,
                            )
                            .unwrap_or_else(|err| {
                                error!(
                                    "Failed to evaluate client selectors for: {:?} Error: {:?}",
                                    pumped_data.relative_moniker, err
                                );
                                Vec::new()
                            });

                        if matching_selectors.is_empty() {
                            None
                        } else {
                            match (&matching_selectors).try_into() {
                                Ok(hierarchy_matcher) => Some(hierarchy_matcher),
                                Err(e) => {
                                    error!("Failed to create hierarchy matcher: {:?}", e);
                                    None
                                }
                            }
                        }
                    });

                // If there were configured matchers and none of them matched
                // this component, then we should return early since there is no data to
                // extract.
                if configured_matchers.is_none() {
                    return acc;
                }
            }

            let component_url = pumped_data.component_url;
            let mut result = ReaderServer::filter_single_components_snapshots(
                sanitized_moniker.clone(),
                pumped_data.snapshots,
                pumped_data.inspect_matcher,
                &client_selector_matches,
            )
            .into_iter()
            .map(|hierarchy_data| BatchResultItem {
                moniker: sanitized_moniker.clone(),
                component_url: component_url.clone(),
                hierarchy_data,
            })
            .collect();

            acc.append(&mut result);
            acc
        })
    }

    /// Takes a vector of HierarchyData structs, and a `fidl_fuchsia_diagnostics/Format`
    /// enum, and writes each diagnostics hierarchy into a READ_ONLY VMO according to
    /// provided format. This VMO is then packaged into a `fidl_fuchsia_mem/Buffer`
    /// which is then packaged into a `fidl_fuchsia_diagnostics/FormattedContent`
    /// xunion which specifies the format of the VMO for clients.
    ///
    /// Errors in the returned Vector correspond to IO failures in writing to a VMO. If
    /// a node hierarchy fails to format, its vmo is an empty string.
    fn format_hierarchies(
        format: &fidl_fuchsia_diagnostics::Format,
        batch_results: Vec<BatchResultItem>,
    ) -> Vec<Result<fidl_fuchsia_diagnostics::FormattedContent, Error>> {
        batch_results
            .into_iter()
            .map(|batch_item| {
                let formatted_string_result = match format {
                    fidl_fuchsia_diagnostics::Format::Json => {
                        let inspect_schema = Schema::for_inspect(
                            batch_item.moniker,
                            batch_item.hierarchy_data.hierarchy,
                            batch_item.hierarchy_data.timestamp,
                            batch_item.component_url,
                            batch_item.hierarchy_data.filename,
                            batch_item.hierarchy_data.errors,
                        );

                        Ok(serde_json::to_string_pretty(&inspect_schema)?)
                    }
                    fidl_fuchsia_diagnostics::Format::Text => {
                        Err(format_err!("Text formatting not supported for inspect."))
                    }
                };

                let content_string = match formatted_string_result {
                    Ok(formatted_string) => formatted_string,
                    Err(e) => {
                        // TODO(4601): Convert failed formattings into the
                        // canonical json schema, with a failure message in "data"
                        error!("parsing results from the inspect source failed: {:?}", e);
                        "".to_string()
                    }
                };

                let vmo_size: u64 = content_string.len() as u64;

                let dump_vmo_result: Result<zx::Vmo, Error> = zx::Vmo::create(vmo_size as u64)
                    .map_err(|s| format_err!("error creating buffer, zx status: {}", s));

                dump_vmo_result.and_then(|dump_vmo| {
                    dump_vmo
                        .write(content_string.as_bytes(), 0)
                        .map_err(|s| format_err!("error writing buffer, zx status: {}", s))?;

                    let client_vmo =
                        dump_vmo.duplicate_handle(zx::Rights::READ | zx::Rights::BASIC)?;

                    let mem_buffer = fidl_fuchsia_mem::Buffer { vmo: client_vmo, size: vmo_size };

                    match format {
                        fidl_fuchsia_diagnostics::Format::Json => {
                            Ok(fidl_fuchsia_diagnostics::FormattedContent::Json(mem_buffer))
                        }
                        fidl_fuchsia_diagnostics::Format::Text => {
                            Ok(fidl_fuchsia_diagnostics::FormattedContent::Json(mem_buffer))
                        }
                    }
                })
            })
            .collect()
    }

    /// Takes a BatchIterator server channel and upon receiving a GetNext request, serves
    /// an empty vector denoting that the iterator has reached its end and is terminating.
    ///
    /// NOTE: Once the server is on the "terminal" send, it will continue to send the terminal
    /// batch until the client terminates their connection.
    pub async fn serve_terminal_batch(
        &self,
        stream: &mut BatchIteratorRequestStream,
    ) -> Result<(), Error> {
        if stream.is_terminated() {
            return Ok(());
        }

        while let Some(req) = stream.try_next().await? {
            match req {
                fidl_fuchsia_diagnostics::BatchIteratorRequest::GetNext { responder } => {
                    self.inspect_reader_server_stats
                        .global_stats
                        .batch_iterator_get_next_requests
                        .add(1);
                    self.inspect_reader_server_stats
                        .global_stats
                        .batch_iterator_get_next_responses
                        .add(1);
                    self.inspect_reader_server_stats.batch_iterator_get_next_requests.add(1);
                    self.inspect_reader_server_stats.batch_iterator_get_next_responses.add(1);
                    self.inspect_reader_server_stats.batch_iterator_terminal_responses.add(1);
                    responder.send(&mut Ok(Vec::new()))?;
                }
            }
        }
        Ok(())
    }

    // Checks a data container for constraints the system places, and if the container is
    // in violation, replaces the container with an error container.
    // Known violations:
    //      TODO(53795): Relax this violation to support diagnostics sources of any count.
    //   1) Hosting more than 64 sources of diagnostics data.
    fn sanitize_populated_data_container(
        container: PopulatedInspectDataContainer,
    ) -> PopulatedInspectDataContainer {
        // Convert VMOs that contain too many snapshots to be sent into
        // an error schema explaining what wat went wrong.
        if container.snapshots.len() > IN_MEMORY_SNAPSHOT_LIMIT {
            let no_success_snapshot_data = vec![SnapshotData::failed(
                formatter::Error {
                    message: format!(
                        concat!(
                            "Platform cannot exfiltrate >64 diagnostics",
                            " sources per component.: {:?}, see fxb/53795 for details."
                        ),
                        container.relative_moniker.clone()
                    ),
                },
                "NO_FILE_SUCCEEDED".to_string(),
            )];
            PopulatedInspectDataContainer {
                relative_moniker: container.relative_moniker,
                component_url: container.component_url,
                snapshots: no_success_snapshot_data,
                inspect_matcher: container.inspect_matcher,
            }
        } else {
            container
        }
    }

    /// Takes a BatchIterator server channel and starts serving snapshotted
    /// Inspect hierarchies to clients as vectors of FormattedContent. The hierarchies
    /// are served in batches of `IN_MEMORY_SNAPSHOT_LIMIT` at a time, and snapshots of
    /// diagnostics data aren't taken until a component is included in the upcoming batch.
    ///
    /// NOTE: This API does not send the terminal empty-vector at the end of the snapshot.
    pub async fn serve_inspect_snapshot(
        &self,
        stream: &mut BatchIteratorRequestStream,
        format: &fidl_fuchsia_diagnostics::Format,
    ) -> Result<(), Error> {
        if stream.is_terminated() {
            return Ok(());
        }

        // We must fetch the repositories in a closure to prevent the
        // repository mutex-guard from leaking into futures.
        let inspect_repo_data = self.inspect_repo.read().fetch_inspect_data();

        let inspect_repo_length = inspect_repo_data.len();
        let mut inspect_repo_iter = inspect_repo_data.into_iter();
        let mut iter = 0;
        let max = (inspect_repo_length - 1 / IN_MEMORY_SNAPSHOT_LIMIT) + 1;
        let mut overflow_bucket: Vec<PopulatedInspectDataContainer> = Vec::new();

        while let Some(req) = stream.try_next().await? {
            match req {
                fidl_fuchsia_diagnostics::BatchIteratorRequest::GetNext { responder } => {
                    self.inspect_reader_server_stats
                        .global_stats
                        .batch_iterator_get_next_requests
                        .add(1);
                    self.inspect_reader_server_stats.batch_iterator_get_next_requests.add(1);
                    loop {
                        // Only retrieve new data from the repository if the overflow bucket
                        // is empty.
                        let pumped_inspect_data = if overflow_bucket.is_empty() {
                            let snapshot_batch: Vec<UnpopulatedInspectDataContainer> =
                                (&mut inspect_repo_iter).take(IN_MEMORY_SNAPSHOT_LIMIT).collect();

                            iter = iter + 1;

                            // Asynchronously populate data containers with snapshots
                            // of relevant inspect hierarchies.
                            self.pump_inspect_data(snapshot_batch).await
                        } else {
                            std::mem::take(&mut overflow_bucket)
                        };

                        let mut current_batch = Vec::new();
                        let mut snapshot_count = 0;
                        for populated_data_container in pumped_inspect_data {
                            let populated_data_container =
                                ReaderServer::sanitize_populated_data_container(
                                    populated_data_container,
                                );
                            let snapshots_in_container = populated_data_container.snapshots.len();

                            if (snapshot_count + snapshots_in_container) > IN_MEMORY_SNAPSHOT_LIMIT
                            {
                                overflow_bucket.push(populated_data_container);
                            } else {
                                current_batch.push(populated_data_container);
                                snapshot_count += snapshots_in_container;
                            }
                        }

                        if current_batch.is_empty() {
                            // Either nothing remains in the repository, or the
                            // only remaining things have more than 64 data sources.
                            self.inspect_reader_server_stats
                                .batch_iterator_terminal_responses
                                .add(1);
                            self.inspect_reader_server_stats
                                .batch_iterator_get_next_responses
                                .add(1);
                            self.inspect_reader_server_stats
                                .global_stats
                                .batch_iterator_get_next_responses
                                .add(1);
                            responder.send(&mut Ok(Vec::new()))?;
                            return Ok(());
                        }

                        // Apply selector filtering to all snapshot inspect hierarchies in
                        // the batch
                        let batch_hierarchy_data = self.filter_snapshots(current_batch);

                        batch_hierarchy_data.iter().for_each(|batch_item| {
                            if !batch_item.hierarchy_data.errors.is_empty() {
                                self.inspect_reader_server_stats
                                    .global_stats
                                    .batch_iterator_get_next_result_errors
                                    .add(1);
                            }
                            self.inspect_reader_server_stats
                                .global_stats
                                .batch_iterator_get_next_result_count
                                .add(1);
                        });

                        let formatted_content: Vec<
                            Result<fidl_fuchsia_diagnostics::FormattedContent, Error>,
                        > = ReaderServer::format_hierarchies(format, batch_hierarchy_data);

                        let filtered_results: Vec<fidl_fuchsia_diagnostics::FormattedContent> =
                            formatted_content.into_iter().filter_map(Result::ok).collect();

                        // We had data in the current_batch but it all got filtered away.
                        // We shouldn't send the terminal batch since we don't know if the
                        // repository is empty! We should try again from the top.
                        if filtered_results.is_empty() {
                            continue;
                        }

                        self.inspect_reader_server_stats
                            .global_stats
                            .batch_iterator_get_next_responses
                            .add(1);
                        self.inspect_reader_server_stats.batch_iterator_get_next_responses.add(1);
                        responder.send(&mut Ok(filtered_results))?;
                        break;
                    }
                }
            }

            // We've sent all the meaningful content available in snapshot mode.
            // The terminal value must be handled separately.
            if iter == max - 1 {
                break;
            }
        }
        Ok(())
    }

    pub fn stream_inspect(
        self,
        stream_mode: fidl_fuchsia_diagnostics::StreamMode,
        format: fidl_fuchsia_diagnostics::Format,
        result_stream: ServerEnd<BatchIteratorMarker>,
    ) -> Result<(), Error> {
        let result_channel = fasync::Channel::from_channel(result_stream.into_channel())?;

        // Self isn't guaranteed to live into the exception handling of the async block. We need to clone self
        // to have a version that can be referenced in the exception handling.
        let errorful_inspect_reader_server_stats = self.inspect_reader_server_stats.clone();
        fasync::spawn(
            async move {
                self.inspect_reader_server_stats
                    .global_stats
                    .inspect_batch_iterator_connections_opened
                    .add(1);

                let mut iterator_req_stream =
                    fidl_fuchsia_diagnostics::BatchIteratorRequestStream::from_channel(
                        result_channel,
                    );

                if stream_mode == fidl_fuchsia_diagnostics::StreamMode::Snapshot
                    || stream_mode == fidl_fuchsia_diagnostics::StreamMode::SnapshotThenSubscribe
                {
                    self.serve_inspect_snapshot(&mut iterator_req_stream, &format).await?;
                }

                if stream_mode == fidl_fuchsia_diagnostics::StreamMode::Subscribe
                    || stream_mode == fidl_fuchsia_diagnostics::StreamMode::SnapshotThenSubscribe
                {
                    error!("not yet supported");
                }

                self.serve_terminal_batch(&mut iterator_req_stream).await?;
                self.inspect_reader_server_stats
                    .global_stats
                    .inspect_batch_iterator_connections_closed
                    .add(1);
                Ok(())
            }
            .unwrap_or_else(move |e: anyhow::Error| {
                errorful_inspect_reader_server_stats
                    .global_stats
                    .batch_iterator_get_next_errors
                    .add(1);
                errorful_inspect_reader_server_stats
                    .global_stats
                    .inspect_batch_iterator_connections_closed
                    .add(1);
                error!("Error encountered running inspect stream: {:?}", e);
            }),
        );

        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use {
        super::*,
        crate::{
            data_repository::{DataCollector, InspectDataCollector},
            events::types::{ComponentIdentifier, InspectData, LegacyIdentifier, RealmPath},
        },
        fdio,
        fidl::endpoints::create_proxy,
        fidl::endpoints::DiscoverableService,
        fidl_fuchsia_inspect::TreeMarker,
        fidl_fuchsia_io::DirectoryMarker,
        fuchsia_async::{self as fasync, DurationExt},
        fuchsia_component::server::ServiceFs,
        fuchsia_inspect::{assert_inspect_tree, reader, Inspector},
        fuchsia_inspect_node_hierarchy::{trie::TrieIterableNode, NodeHierarchy},
        fuchsia_zircon as zx,
        fuchsia_zircon::Peered,
        futures::{FutureExt, StreamExt},
        serde_json::json,
        std::path::PathBuf,
    };

    const TEST_URL: &'static str = "fuchsia-pkg://test";

    fn get_vmo(text: &[u8]) -> zx::Vmo {
        let vmo = zx::Vmo::create(4096).unwrap();
        vmo.write(text, 0).unwrap();
        vmo
    }

    #[fasync::run_singlethreaded(test)]
    async fn inspect_data_collector() {
        let path = PathBuf::from("/test-bindings");
        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let vmo = get_vmo(b"test1");
        let vmo2 = get_vmo(b"test2");
        let vmo3 = get_vmo(b"test3");
        let vmo4 = get_vmo(b"test4");
        fs.dir("diagnostics").add_vmo_file_at("root.inspect", vmo, 0, 4096);
        fs.dir("diagnostics").add_vmo_file_at("root_not_inspect", vmo2, 0, 4096);
        fs.dir("diagnostics").dir("a").add_vmo_file_at("root.inspect", vmo3, 0, 4096);
        fs.dir("diagnostics").dir("b").add_vmo_file_at("root.inspect", vmo4, 0, 4096);
        // Create a connection to the ServiceFs.
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::spawn(fs.collect());

        let (done0, done1) = zx::Channel::create().unwrap();

        let thread_path = path.join("out/diagnostics");

        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();

            executor.run_singlethreaded(async {
                let collector = InspectDataCollector::new();
                // Trigger collection on a clone of the inspect collector so
                // we can use collector to take the collected data.
                Box::new(collector.clone()).collect(path).await.unwrap();
                let collector: Box<InspectDataCollector> = Box::new(collector);

                let extra_data = collector.take_data().expect("collector missing data");
                assert_eq!(3, extra_data.len());

                let assert_extra_data = |path: &str, content: &[u8]| {
                    let extra = extra_data.get(path);
                    assert!(extra.is_some());

                    match extra.unwrap() {
                        InspectData::Vmo(vmo) => {
                            let mut buf = [0u8; 5];
                            vmo.read(&mut buf, 0).expect("reading vmo");
                            assert_eq!(content, &buf);
                        }
                        v => {
                            panic!("Expected Vmo, got {:?}", v);
                        }
                    }
                };

                assert_extra_data("root.inspect", b"test1");
                assert_extra_data("a/root.inspect", b"test3");
                assert_extra_data("b/root.inspect", b"test4");

                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn inspect_data_collector_tree() {
        let path = PathBuf::from("/test-bindings2");

        // Make a ServiceFs serving an inspect tree.
        let mut fs = ServiceFs::new();
        let inspector = Inspector::new();
        inspector.root().record_int("a", 1);
        inspector.root().record_lazy_child("lazy", || {
            async move {
                let inspector = Inspector::new();
                inspector.root().record_double("b", 3.14);
                Ok(inspector)
            }
            .boxed()
        });
        inspector.serve(&mut fs).expect("failed to serve inspector");

        // Create a connection to the ServiceFs.
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::spawn(fs.collect());

        let (done0, done1) = zx::Channel::create().unwrap();
        let thread_path = path.join("out/diagnostics");

        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();

            executor.run_singlethreaded(async {
                let collector = InspectDataCollector::new();

                //// Trigger collection on a clone of the inspect collector so
                //// we can use collector to take the collected data.
                Box::new(collector.clone()).collect(path).await.unwrap();
                let collector: Box<InspectDataCollector> = Box::new(collector);

                let extra_data = collector.take_data().expect("collector missing data");
                assert_eq!(1, extra_data.len());

                let extra = extra_data.get(TreeMarker::SERVICE_NAME);
                assert!(extra.is_some());

                match extra.unwrap() {
                    InspectData::Tree(tree, vmo) => {
                        // Assert we can read the tree proxy and get the data we expected.
                        let hierarchy = reader::read_from_tree(&tree)
                            .await
                            .expect("failed to read hierarchy from tree");
                        assert_inspect_tree!(hierarchy, root: {
                            a: 1i64,
                            lazy: {
                                b: 3.14,
                            }
                        });
                        let partial_hierarchy: NodeHierarchy =
                            PartialNodeHierarchy::try_from(vmo.as_ref().unwrap())
                                .expect("failed to read hierarchy from vmo")
                                .into();
                        // Assert the vmo also points to that data (in this case since there's no
                        // lazy nodes).
                        assert_inspect_tree!(partial_hierarchy, root: {
                            a: 1i64,
                        });
                    }
                    v => {
                        panic!("Expected Tree, got {:?}", v);
                    }
                }

                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn reader_server_formatting() {
        let path = PathBuf::from("/test-bindings3");

        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let vmo = zx::Vmo::create(4096).unwrap();
        let inspector = inspector_for_reader_test();

        let data = inspector.copy_vmo_data().unwrap();
        vmo.write(&data, 0).unwrap();
        fs.dir("diagnostics").add_vmo_file_at("test.inspect", vmo, 0, 4096);

        // Create a connection to the ServiceFs.
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::spawn(fs.collect());
        let (done0, done1) = zx::Channel::create().unwrap();
        let thread_path = path.join("out");

        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();
            executor.run_singlethreaded(async {
                verify_reader(path).await;
                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn read_server_formatting_tree() {
        let path = PathBuf::from("/test-bindings4");

        // Make a ServiceFs containing two files.
        // One is an inspect file, and one is not.
        let mut fs = ServiceFs::new();
        let inspector = inspector_for_reader_test();
        inspector.serve(&mut fs).expect("failed to serve inspector");

        // Create a connection to the ServiceFs.
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::spawn(fs.collect());
        let (done0, done1) = zx::Channel::create().unwrap();
        let thread_path = path.join("out");

        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();
            executor.run_singlethreaded(async {
                verify_reader(path).await;
                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });
        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn reader_server_reports_errors() {
        let path = PathBuf::from("/test-bindings-errors-01");

        // Make a ServiceFs containing something that looks like an inspect file but is not.
        let mut fs = ServiceFs::new();
        let vmo = zx::Vmo::create(4096).unwrap();
        fs.dir("diagnostics").add_vmo_file_at("test.inspect", vmo, 0, 4096);

        // Create a connection to the ServiceFs.
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.join("out").to_str().unwrap(), h0).unwrap();

        fasync::spawn(fs.collect());
        let (done0, done1) = zx::Channel::create().unwrap();
        let thread_path = path.join("out");

        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let path = thread_path;
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();
            executor.run_singlethreaded(async {
                verify_reader_with_mode(path, VerifyMode::ExpectComponentFailure).await;
                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.join("out").to_str().unwrap()).unwrap();
    }

    #[fasync::run_singlethreaded(test)]
    async fn inspect_repo_disallows_duplicated_dirs() {
        let mut inspect_repo = DiagnosticsDataRepository::new(None);
        let realm_path = RealmPath(vec!["a".to_string(), "b".to_string()]);
        let instance_id = "1234".to_string();

        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id,
            realm_path,
            component_name: "foo.cmx".into(),
        });
        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");
        inspect_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy)
            .expect("add to repo");

        let (proxy, _) =
            fidl::endpoints::create_proxy::<DirectoryMarker>().expect("create directory proxy");
        inspect_repo
            .add_inspect_artifacts(component_id.clone(), TEST_URL, proxy)
            .expect("add to repo");

        let key = component_id.unique_key();
        assert_eq!(inspect_repo.data_directories.get(key).unwrap().get_values().len(), 1);
    }

    #[fasync::run_singlethreaded(test)]
    async fn canonical_inspect_reader_stress_test() {
        // Test that 3 directories, each with 33 vmos, has snapshots served over 3 batches
        // each of which contains the 33 vmos of one component.
        stress_test_diagnostics_repository(vec![33, 33, 33], vec![33, 33, 33]).await;

        // The 64 entry vmo is served by itself, and the 63 vmo and 1 vmo directories are combined.
        stress_test_diagnostics_repository(vec![64, 63, 1], vec![64, 64]).await;

        // 64 1vmo components are sent in one batch.
        stress_test_diagnostics_repository([1usize; 64].to_vec(), vec![64]).await;

        // A 65+ vmo component will manifest as an error schema.
        // TODO(53795): Support streaming arbitrary diagnostics source counts from
        // components using mpsc pipelines.
        stress_test_diagnostics_repository(vec![65], vec![1]).await;

        // An errorful component doesn't halt iteration.
        stress_test_diagnostics_repository(vec![64, 65, 64, 64], vec![64, 1, 64, 64]).await;

        // An errorful component can be merged into a batch where it may fit.
        stress_test_diagnostics_repository(vec![63, 65], vec![64]).await;
    }

    async fn stress_test_diagnostics_repository(
        directory_vmo_counts: Vec<usize>,
        expected_batch_results: Vec<usize>,
    ) {
        let path = PathBuf::from("/stress_test_root_directory");

        let dir_name_and_filecount: Vec<(String, usize)> = directory_vmo_counts
            .into_iter()
            .enumerate()
            .map(|(index, filecount)| (format!("diagnostics_{}", index), filecount))
            .collect();

        // Make a ServiceFs that will host inspect vmos under each
        // of the new diagnostics directories.
        let mut fs = ServiceFs::new();

        let inspector = inspector_for_reader_test();

        for (directory_name, filecount) in dir_name_and_filecount.clone() {
            for i in 0..filecount {
                let vmo = inspector
                    .duplicate_vmo()
                    .ok_or(format_err!("Failed to duplicate VMO"))
                    .unwrap();

                let size = vmo.get_size().unwrap();
                fs.dir(directory_name.clone()).add_vmo_file_at(
                    format!("root_{}.inspect", i),
                    vmo,
                    0, /* vmo offset */
                    size,
                );
            }
        }
        let (h0, h1) = zx::Channel::create().unwrap();
        fs.serve_connection(h1).unwrap();

        // We bind the root of the FS that hosts our 3 test dirs to
        // stress_test_root_dir. Now each directory can be found at
        // stress_test_root_dir/diagnostics_<i>
        let ns = fdio::Namespace::installed().unwrap();
        ns.bind(path.to_str().unwrap(), h0).unwrap();

        fasync::spawn(fs.collect());

        let (done0, done1) = zx::Channel::create().unwrap();

        let cloned_path = path.clone();
        // Run the actual test in a separate thread so that it does not block on FS operations.
        // Use signalling on a zx::Channel to indicate that the test is done.
        std::thread::spawn(move || {
            let done = done1;
            let mut executor = fasync::Executor::new().unwrap();

            executor.run_singlethreaded(async {
                let id_and_directory_proxy =
                    join_all(dir_name_and_filecount.iter().map(|(dir, _)| {
                        let new_async_clone = cloned_path.clone();
                        async move {
                            let full_path = new_async_clone.join(dir);
                            let proxy = InspectDataCollector::find_directory_proxy(&full_path)
                                .await
                                .unwrap();
                            let unique_cid = ComponentIdentifier::Legacy(LegacyIdentifier {
                                instance_id: "1234".into(),
                                realm_path: vec![].into(),
                                component_name: format!("component_{}.cmx", dir).into(),
                            });
                            (unique_cid, proxy)
                        }
                    }))
                    .await;

                let inspect_repo = Arc::new(RwLock::new(DiagnosticsDataRepository::new(None)));

                for (cid, proxy) in id_and_directory_proxy {
                    inspect_repo
                        .write()
                        .add_inspect_artifacts(cid.clone(), TEST_URL, proxy)
                        .unwrap();
                }

                let inspector = Inspector::new();
                let root = inspector.root();
                let test_archive_accessor_node = root.create_child("test_archive_accessor_node");

                let test_accessor_stats =
                    Arc::new(diagnostics::ArchiveAccessorStats::new(test_archive_accessor_node));
                let test_batch_iterator_stats1 = Arc::new(
                    diagnostics::InspectReaderServerStats::new(test_accessor_stats.clone()),
                );

                let reader_server = ReaderServer::new(
                    inspect_repo.clone(),
                    None,
                    test_batch_iterator_stats1.clone(),
                );
                let _result_json = read_snapshot_verify_batch_count_and_batch_size(
                    reader_server,
                    expected_batch_results,
                )
                .await;

                done.signal_peer(zx::Signals::NONE, zx::Signals::USER_0).expect("signalling peer");
            });
        });

        fasync::OnSignals::new(&done0, zx::Signals::USER_0).await.unwrap();
        ns.unbind(path.to_str().unwrap()).unwrap();
    }

    fn inspector_for_reader_test() -> Inspector {
        let inspector = Inspector::new();
        let root = inspector.root();
        let child_1 = root.create_child("child_1");
        child_1.record_int("some-int", 2);
        let child_1_1 = child_1.create_child("child_1_1");
        child_1_1.record_int("some-int", 3);
        child_1_1.record_int("not-wanted-int", 4);
        root.record(child_1_1);
        root.record(child_1);
        let child_2 = root.create_child("child_2");
        child_2.record_int("some-int", 2);
        root.record(child_2);
        inspector
    }

    enum VerifyMode {
        ExpectSuccess,
        ExpectComponentFailure,
    }

    async fn verify_reader(path: PathBuf) {
        verify_reader_with_mode(path, VerifyMode::ExpectSuccess).await;
    }

    async fn verify_reader_with_mode(path: PathBuf, mode: VerifyMode) {
        let child_1_1_selector = selectors::parse_selector(r#"*:root/child_1/*:some-int"#).unwrap();
        let child_2_selector =
            selectors::parse_selector(r#"test_component.cmx:root/child_2:*"#).unwrap();
        let inspect_repo = Arc::new(RwLock::new(DiagnosticsDataRepository::new(Some(vec![
            Arc::new(child_1_1_selector),
            Arc::new(child_2_selector),
        ]))));

        let out_dir_proxy = InspectDataCollector::find_directory_proxy(&path).await.unwrap();

        // The absolute moniker here is made up since the selector is a glob
        // selector, so any path would match.
        let component_id = ComponentIdentifier::Legacy(LegacyIdentifier {
            instance_id: "1234".into(),
            realm_path: vec![].into(),
            component_name: "test_component.cmx".into(),
        });

        let inspector = Inspector::new();
        let root = inspector.root();
        let test_archive_accessor_node = root.create_child("test_archive_accessor_node");

        assert_inspect_tree!(inspector, root: {test_archive_accessor_node: {}});

        let test_accessor_stats =
            Arc::new(diagnostics::ArchiveAccessorStats::new(test_archive_accessor_node));

        let test_batch_iterator_stats1 =
            Arc::new(diagnostics::InspectReaderServerStats::new(test_accessor_stats.clone()));

        assert_inspect_tree!(inspector, root: {test_archive_accessor_node: {
        archive_accessor_connections_opened: 0u64,
        archive_accessor_connections_closed: 0u64,
        inspect_reader_servers_constructed: 0u64,
            inspect_reader_servers_destroyed: 0u64,
            component_timeouts_count: 0u64,
        stream_diagnostics_requests: 0u64,
        inspect_batch_iterator_connections_opened: 0u64,
        inspect_batch_iterator_connections_closed: 0u64,
        batch_iterator_get_next_requests: 0u64,
        batch_iterator_get_next_responses: 0u64,
        batch_iterator_get_next_errors: 0u64,
        batch_iterator_get_next_result_count: 0u64,
        batch_iterator_get_next_result_errors: 0u64,
        batch_iterator_connection0: {
            batch_iterator_terminal_responses: 0u64,
            batch_iterator_get_next_responses: 0u64,
            batch_iterator_get_next_requests: 0u64,
        }}});

        let inspector_arc = Arc::new(inspector);
        inspect_repo
            .write()
            .add_inspect_artifacts(component_id.clone(), TEST_URL, out_dir_proxy)
            .unwrap();

        let expected_get_next_result_errors = match mode {
            VerifyMode::ExpectComponentFailure => 1u64,
            _ => 0u64,
        };

        {
            let reader_server =
                ReaderServer::new(inspect_repo.clone(), None, test_batch_iterator_stats1.clone());

            let result_json = read_snapshot(reader_server, inspector_arc.clone()).await;

            let result_array = result_json.as_array().expect("unit test json should be array.");
            assert_eq!(result_array.len(), 1, "Expect only one schema to be returned.");

            let result_map =
                result_array[0].as_object().expect("entries in the schema array are json objects.");

            let result_payload =
                result_map.get("payload").expect("diagnostics schema requires payload entry.");

            let expected_payload = match mode {
                VerifyMode::ExpectSuccess => json!({
                    "root": {
                        "child_1": {
                            "child_1_1": {
                                "some-int": 3
                            }
                        },
                        "child_2": {
                            "some-int": 2
                        }
                    }
                }),
                VerifyMode::ExpectComponentFailure => json!(null),
            };
            assert_eq!(*result_payload, expected_payload);

            // stream_diagnostics_requests is 0 since its tracked via archive_accessor server,
            // which isnt running in this unit test.
            assert_inspect_tree!(inspector_arc.clone(), root: {test_archive_accessor_node: {
            archive_accessor_connections_opened: 0u64,
            archive_accessor_connections_closed: 0u64,
            inspect_reader_servers_constructed: 1u64,
            inspect_reader_servers_destroyed: 0u64,
                stream_diagnostics_requests: 0u64,
                component_timeouts_count: 0u64,
            inspect_batch_iterator_connections_opened: 1u64,
            inspect_batch_iterator_connections_closed: 0u64,
            batch_iterator_get_next_requests: 2u64,
            batch_iterator_get_next_responses: 2u64,
            batch_iterator_get_next_errors: 0u64,
            batch_iterator_get_next_result_count: 1u64,
            batch_iterator_get_next_result_errors: expected_get_next_result_errors,
            batch_iterator_connection0: {
                batch_iterator_terminal_responses: 1u64,
                batch_iterator_get_next_responses: 2u64,
                batch_iterator_get_next_requests: 2u64,
            }}});
        }

        // There is a race between the RAII destruction of the reader server which must make
        // one more try_next call after the client channel is destroyed at the end of read_snapshot
        // and the inspector seeing both that reader server desruction and the termination of the
        // batch iterator connection.
        wait_for_reader_service_cleanup(inspector_arc.clone(), 1).await;

        // we should see that the reader server has been destroyed.
        assert_inspect_tree!(inspector_arc.clone(), root: {test_archive_accessor_node: {
        archive_accessor_connections_opened: 0u64,
        archive_accessor_connections_closed: 0u64,
        inspect_reader_servers_constructed: 1u64,
        inspect_reader_servers_destroyed: 1u64,
            stream_diagnostics_requests: 0u64,
                            component_timeouts_count: 0u64,
        inspect_batch_iterator_connections_opened: 1u64,
        inspect_batch_iterator_connections_closed: 1u64,
        batch_iterator_get_next_requests: 2u64,
        batch_iterator_get_next_responses: 2u64,
        batch_iterator_get_next_errors: 0u64,
        batch_iterator_get_next_result_count: 1u64,
        batch_iterator_get_next_result_errors: expected_get_next_result_errors,
        batch_iterator_connection0: {
            batch_iterator_terminal_responses: 1u64,
            batch_iterator_get_next_responses: 2u64,
            batch_iterator_get_next_requests: 2u64,
        }}});

        let test_batch_iterator_stats2 =
            Arc::new(diagnostics::InspectReaderServerStats::new(test_accessor_stats.clone()));

        inspect_repo.write().remove(&component_id);
        {
            let reader_server =
                ReaderServer::new(inspect_repo.clone(), None, test_batch_iterator_stats2.clone());
            let result_json = read_snapshot(reader_server, inspector_arc.clone()).await;

            let result_array = result_json.as_array().expect("unit test json should be array.");
            assert_eq!(result_array.len(), 0, "Expect no schemas to be returned.");

            assert_inspect_tree!(inspector_arc.clone(), root: {test_archive_accessor_node: {
            archive_accessor_connections_opened: 0u64,
            archive_accessor_connections_closed: 0u64,
            inspect_reader_servers_constructed: 2u64,
            inspect_reader_servers_destroyed: 1u64,
                stream_diagnostics_requests: 0u64,
                component_timeouts_count: 0u64,
            inspect_batch_iterator_connections_opened: 2u64,
            inspect_batch_iterator_connections_closed: 1u64,
            batch_iterator_get_next_requests: 3u64,
            batch_iterator_get_next_responses: 3u64,
            batch_iterator_get_next_errors: 0u64,
            batch_iterator_get_next_result_count: 1u64,
            batch_iterator_get_next_result_errors: expected_get_next_result_errors,
            batch_iterator_connection0: {
                batch_iterator_terminal_responses: 1u64,
                batch_iterator_get_next_responses: 2u64,
                batch_iterator_get_next_requests: 2u64,
            },
            batch_iterator_connection1: {
                batch_iterator_terminal_responses: 1u64,
                batch_iterator_get_next_responses: 1u64,
                batch_iterator_get_next_requests: 1u64,
            }}});
        }

        // There is a race between the RAII destruction of the reader server which must make
        // one more try_next call after the client channel is destroyed at the end of read_snapshot
        // and the inspector seeing both that reader server desruction and the termination of the
        // batch iterator connection.
        wait_for_reader_service_cleanup(inspector_arc.clone(), 2).await;

        assert_inspect_tree!(inspector_arc.clone(), root: {test_archive_accessor_node: {
        archive_accessor_connections_opened: 0u64,
        archive_accessor_connections_closed: 0u64,
        inspect_reader_servers_constructed: 2u64,
        inspect_reader_servers_destroyed: 2u64,
        stream_diagnostics_requests: 0u64,
        inspect_batch_iterator_connections_opened: 2u64,
            inspect_batch_iterator_connections_closed: 2u64,
            component_timeouts_count: 0u64,
        batch_iterator_get_next_requests: 3u64,
        batch_iterator_get_next_responses: 3u64,
        batch_iterator_get_next_errors: 0u64,
        batch_iterator_get_next_result_count: 1u64,
        batch_iterator_get_next_result_errors: expected_get_next_result_errors,
        batch_iterator_connection0: {
            batch_iterator_terminal_responses: 1u64,
            batch_iterator_get_next_responses: 2u64,
            batch_iterator_get_next_requests: 2u64,
        },
        batch_iterator_connection1: {
            batch_iterator_terminal_responses: 1u64,
            batch_iterator_get_next_responses: 1u64,
            batch_iterator_get_next_requests: 1u64,
        }}});
    }

    async fn wait_for_reader_service_cleanup(
        inspector: Arc<Inspector>,
        expected_destroyed_reader_servers: u64,
    ) {
        loop {
            let inspect_hierarchy = reader::read_from_inspector(&inspector)
                .await
                .expect("test inspector should be parseable.");
            let destroyed_readers_selector = selectors::parse_selector(
                r#"*:root/test_archive_accessor_node:inspect_reader_servers_destroyed"#,
            )
            .unwrap();

            match fuchsia_inspect_node_hierarchy::select_from_node_hierarchy(
                    inspect_hierarchy,
                    destroyed_readers_selector,
                )
                .expect("Always expect selection of inspect_reader_servers_destroyed to succeed.")
                .as_slice()
                {
                    [destroyed_reader_property_entry] => {
                        match destroyed_reader_property_entry.property {
                            fuchsia_inspect_node_hierarchy::Property::Uint(_, x) => {
                                if x == expected_destroyed_reader_servers {
                                    break;
                                } else {
                                    let sleep_duration = zx::Duration::from_millis(10i64);
                                    fasync::Timer::new(sleep_duration.after_now()).await;
                                    continue;
                                }
                            },
                            _ => panic!("inspect_reader_servers_destroyed should always be a uint."),
                        }
                    },
                    _ => panic!("Test always expects exactly one inspect_reader_servers_destroyed property to be present."),
                }
        }
    }

    async fn read_snapshot(
        reader_server: ReaderServer,
        _test_inspector: Arc<Inspector>,
    ) -> serde_json::Value {
        let (consumer, batch_iterator): (
            _,
            ServerEnd<fidl_fuchsia_diagnostics::BatchIteratorMarker>,
        ) = create_proxy().unwrap();

        fasync::spawn(async move {
            reader_server
                .stream_inspect(
                    fidl_fuchsia_diagnostics::StreamMode::Snapshot,
                    fidl_fuchsia_diagnostics::Format::Json,
                    batch_iterator,
                )
                .unwrap();
        });

        let mut result_vec: Vec<String> = Vec::new();
        loop {
            let next_batch: Vec<fidl_fuchsia_diagnostics::FormattedContent> =
                consumer.get_next().await.unwrap().unwrap();

            if next_batch.is_empty() {
                break;
            }
            for formatted_content in next_batch {
                match formatted_content {
                    fidl_fuchsia_diagnostics::FormattedContent::Json(data) => {
                        let mut buf = vec![0; data.size as usize];
                        data.vmo.read(&mut buf, 0).expect("reading vmo");
                        let hierarchy_string = std::str::from_utf8(&buf).unwrap();
                        result_vec.push(hierarchy_string.to_string());
                    }
                    _ => panic!("test only produces json formatted data"),
                }
            }
        }
        let result_string = format!("[{}]", result_vec.join(","));
        serde_json::from_str(&result_string)
            .expect(&format!("unit tests shouldn't be creating malformed json: {}", result_string))
    }

    async fn read_snapshot_verify_batch_count_and_batch_size(
        reader_server: ReaderServer,
        mut expected_batch_sizes: Vec<usize>,
    ) -> serde_json::Value {
        let (consumer, batch_iterator): (
            _,
            ServerEnd<fidl_fuchsia_diagnostics::BatchIteratorMarker>,
        ) = create_proxy().unwrap();

        fasync::spawn(async move {
            reader_server
                .stream_inspect(
                    fidl_fuchsia_diagnostics::StreamMode::Snapshot,
                    fidl_fuchsia_diagnostics::Format::Json,
                    batch_iterator,
                )
                .unwrap();
        });

        let mut result_vec: Vec<String> = Vec::new();
        let mut batch_counts = Vec::new();
        loop {
            let next_batch: Vec<fidl_fuchsia_diagnostics::FormattedContent> =
                consumer.get_next().await.unwrap().unwrap();

            if next_batch.is_empty() {
                expected_batch_sizes.sort();
                batch_counts.sort();
                assert_eq!(expected_batch_sizes, batch_counts);
                break;
            }

            batch_counts.push(next_batch.len());

            for formatted_content in next_batch {
                match formatted_content {
                    fidl_fuchsia_diagnostics::FormattedContent::Json(data) => {
                        let mut buf = vec![0; data.size as usize];
                        data.vmo.read(&mut buf, 0).expect("reading vmo");
                        let hierarchy_string = std::str::from_utf8(&buf).unwrap();
                        result_vec.push(hierarchy_string.to_string());
                    }
                    _ => panic!("test only produces json formatted data"),
                }
            }
        }
        let result_string = format!("[{}]", result_vec.join(","));
        serde_json::from_str(&result_string)
            .expect(&format!("unit tests shouldn't be creating malformed json: {}", result_string))
    }
}
