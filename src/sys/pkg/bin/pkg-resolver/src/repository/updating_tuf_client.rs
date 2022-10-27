// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    crate::{
        clock, error, inspect_util, metrics_util::tuf_error_as_update_tuf_client_event_code,
        repository::filesystem_repository::RWRepository, TCP_KEEPALIVE_TIMEOUT,
    },
    anyhow::anyhow,
    cobalt_sw_delivery_registry as metrics,
    fidl_contrib::protocol_connector::ProtocolSender,
    fidl_fuchsia_metrics::MetricEvent,
    fidl_fuchsia_pkg_ext::MirrorConfig,
    fuchsia_async::{self as fasync, TimeoutExt as _},
    fuchsia_cobalt_builders::MetricEventExt as _,
    fuchsia_inspect::{self as inspect, Property},
    fuchsia_inspect_contrib::inspectable::InspectableDebugString,
    fuchsia_zircon as zx,
    futures::{
        future::{AbortHandle, Abortable, FutureExt as _, TryFutureExt as _},
        lock::Mutex as AsyncMutex,
        stream::StreamExt,
    },
    http_uri_ext::HttpUriExt as _,
    std::{
        sync::{Arc, Weak},
        time::Duration,
    },
    tracing::info,
    tuf::{
        error::Error as TufError,
        metadata::{Metadata, TargetDescription, TargetPath},
        pouf::Pouf1,
        repository::{RepositoryProvider, RepositoryStorageProvider},
    },
};

type TufClient = tuf::client::Client<
    Pouf1,
    RWRepository<Pouf1, Box<dyn RepositoryStorageProvider<Pouf1> + Sync + Send>>,
    Box<dyn RepositoryProvider<Pouf1> + Send>,
>;

pub struct UpdatingTufClient {
    client: TufClient,

    /// Time that this repository was last successfully checked for an update, or None if the
    /// repository has never successfully fetched target metadata.
    last_update_successfully_checked_time: InspectableDebugString<Option<zx::Time>>,

    /// `Some` if there is an AutoClient task, dropping it stops the task.
    _auto_client_aborter: Option<AbortHandleOnDrop>,

    tuf_metadata_timeout: Duration,

    inspect: UpdatingTufClientInspectState,

    cobalt_sender: ProtocolSender<MetricEvent>,
}

struct AbortHandleOnDrop {
    abort_handle: AbortHandle,
}

impl Drop for AbortHandleOnDrop {
    fn drop(&mut self) {
        self.abort_handle.abort();
    }
}

impl From<AbortHandle> for AbortHandleOnDrop {
    fn from(abort_handle: AbortHandle) -> Self {
        Self { abort_handle }
    }
}

struct UpdatingTufClientInspectState {
    /// Count of the number of times this repository failed to check for an update.
    update_check_failure_count: inspect_util::Counter,

    /// Count of the number of times this repository was successfully checked for an update.
    update_check_success_count: inspect_util::Counter,

    /// Count of the number of times this repository was successfully updated.
    updated_count: inspect_util::Counter,

    /// Version of the active root file.
    root_version: inspect::UintProperty,

    /// Version of the active timestamp file, or -1 if unknown.
    timestamp_version: inspect::IntProperty,

    /// Version of the active snapshot file, or -1 if unknown.
    snapshot_version: inspect::IntProperty,

    /// Version of the active targets file, or -1 if unknown.
    targets_version: inspect::IntProperty,

    _node: inspect::Node,
}

/// Result of updating metadata if stale.
pub enum UpdateResult {
    /// Metadata update was skipped because it was not stale.
    Deferred,

    /// Local metadata is up to date with the remote.
    UpToDate,

    /// Some metadata was updated.
    Updated,
}

#[derive(Debug)]
pub struct RepoVersions {
    #[allow(dead_code)] // fields present so they're included in the Debug output
    root: u32,
    #[allow(dead_code)]
    timestamp: Option<u32>,
    #[allow(dead_code)]
    snapshot: Option<u32>,
    #[allow(dead_code)]
    targets: Option<u32>,
}

impl UpdatingTufClient {
    pub fn from_tuf_client_and_mirror_config(
        client: TufClient,
        config: Option<&MirrorConfig>,
        tuf_metadata_timeout: Duration,
        node: inspect::Node,
        cobalt_sender: ProtocolSender<MetricEvent>,
    ) -> Arc<AsyncMutex<Self>> {
        let (auto_client_aborter, auto_client_node_and_registration) =
            if config.map_or(false, |c| c.subscribe()) {
                let (aborter, registration) = AbortHandle::new_pair();
                let auto_client_node = node.create_child("auto_client");
                (Some(aborter.into()), Some((auto_client_node, registration, config.unwrap())))
            } else {
                (None, None)
            };
        let root_version = client.database().trusted_root().version();

        let ret = Arc::new(AsyncMutex::new(Self {
            client,
            last_update_successfully_checked_time: InspectableDebugString::new(
                None,
                &node,
                "last_update_successfully_checked_time",
            ),
            _auto_client_aborter: auto_client_aborter,
            tuf_metadata_timeout,
            inspect: UpdatingTufClientInspectState {
                update_check_failure_count: inspect_util::Counter::new(
                    &node,
                    "update_check_failure_count",
                ),
                update_check_success_count: inspect_util::Counter::new(
                    &node,
                    "update_check_success_count",
                ),
                updated_count: inspect_util::Counter::new(&node, "updated_count"),
                root_version: node.create_uint("root_version", root_version.into()),
                timestamp_version: node.create_int("timestamp_version", -1),
                snapshot_version: node.create_int("snapshot_version", -1),
                targets_version: node.create_int("targets_version", -1),
                _node: node,
            },
            cobalt_sender,
        }));

        if let Some((node, registration, mirror_config)) = auto_client_node_and_registration {
            fasync::Task::local(
                Abortable::new(
                    AutoClient::from_updating_client_and_auto_url(
                        Arc::downgrade(&ret),
                        mirror_config
                            .mirror_url()
                            .to_owned()
                            .extend_dir_with_path("auto")
                            // Safe because mirror_url has a scheme and "auto" is a valid path segment.
                            .unwrap()
                            .to_string(),
                        node,
                    )
                    .run(),
                    registration,
                )
                .map(|_| ()),
            )
            .detach();
        }

        ret
    }

    pub(super) fn switch_local_repo_to_write_only_mode(&mut self) {
        self.client.local_repo_mut().switch_to_write_only_mode();
    }

    pub async fn fetch_target_description(
        &mut self,
        target: &TargetPath,
    ) -> Result<TargetDescription, TufError> {
        self.client.fetch_target_description(target).await
    }

    /// Updates the tuf client metadata if it is considered to be stale, returning whether or not
    /// updates were performed.
    pub async fn update_if_stale(&mut self) -> Result<UpdateResult, error::TufOrTimeout> {
        if self.is_stale() {
            if self.update().await? {
                Ok(UpdateResult::Updated)
            } else {
                Ok(UpdateResult::UpToDate)
            }
        } else {
            Ok(UpdateResult::Deferred)
        }
    }

    /// Provides the current known metadata versions.
    pub fn metadata_versions(&self) -> RepoVersions {
        RepoVersions {
            root: self.client.database().trusted_root().version(),
            timestamp: self.client.database().trusted_timestamp().map(|m| m.version()),
            snapshot: self.client.database().trusted_snapshot().map(|m| m.version()),
            targets: self.client.database().trusted_targets().map(|m| m.version()),
        }
    }

    fn is_stale(&self) -> bool {
        if let Some(last_update_time) = *self.last_update_successfully_checked_time {
            last_update_time + METADATA_CACHE_STALE_TIMEOUT <= clock::now()
        } else {
            true
        }
    }

    async fn update(&mut self) -> Result<bool, error::TufOrTimeout> {
        let res = self
            .client
            .update()
            .map_err(error::TufOrTimeout::Tuf)
            .on_timeout(self.tuf_metadata_timeout, || Err(crate::error::TufOrTimeout::Timeout))
            .await;
        self.inspect.root_version.set(self.client.database().trusted_root().version().into());
        self.inspect.timestamp_version.set(
            self.client.database().trusted_timestamp().map(|m| m.version().into()).unwrap_or(-1i64),
        );
        self.inspect.snapshot_version.set(
            self.client.database().trusted_snapshot().map(|m| m.version().into()).unwrap_or(-1i64),
        );
        self.inspect.targets_version.set(
            self.client.database().trusted_targets().map(|m| m.version().into()).unwrap_or(-1i64),
        );
        if let Ok(update_occurred) = &res {
            self.last_update_successfully_checked_time.get_mut().replace(clock::now());
            self.inspect.update_check_success_count.increment();
            if *update_occurred {
                self.inspect.updated_count.increment();
            }
        } else {
            self.inspect.update_check_failure_count.increment();
        }

        self.cobalt_sender.send(
            MetricEvent::builder(metrics::UPDATE_TUF_CLIENT_MIGRATED_METRIC_ID)
                .with_event_codes(match &res {
                    Ok(_) => metrics::UpdateTufClientMigratedMetricDimensionResult::Success,
                    Err(e) => tuf_error_as_update_tuf_client_event_code(e),
                })
                .as_occurrence(1),
        );

        res
    }
}

pub const METADATA_CACHE_STALE_TIMEOUT: zx::Duration = zx::Duration::from_minutes(5);

struct AutoClient {
    updating_client: Weak<AsyncMutex<UpdatingTufClient>>,
    auto_url: String,
    inspect: AutoClientInspectState,
}

struct AutoClientInspectState {
    connect_failure_count: inspect_util::Counter,
    connect_success_count: inspect_util::Counter,
    update_attempt_count: inspect_util::Counter,
    _node: inspect::Node,
}

#[cfg(not(test))]
const AUTO_CLIENT_SSE_RECONNECT_DELAY: zx::Duration = zx::Duration::from_minutes(1);

#[cfg(test)]
const AUTO_CLIENT_SSE_RECONNECT_DELAY: zx::Duration = zx::Duration::from_minutes(0);

impl AutoClient {
    fn from_updating_client_and_auto_url(
        updating_client: Weak<AsyncMutex<UpdatingTufClient>>,
        auto_url: String,
        node: inspect::Node,
    ) -> Self {
        Self {
            updating_client,
            auto_url,
            inspect: AutoClientInspectState {
                connect_failure_count: inspect_util::Counter::new(&node, "connect_failure_count"),
                connect_success_count: inspect_util::Counter::new(&node, "connect_success_count"),
                update_attempt_count: inspect_util::Counter::new(&node, "update_attempt_count"),
                _node: node,
            },
        }
    }

    async fn run(self) {
        // In order to cut down on log spam, only log the first connection attempt and connection
        // error once until we successfully connect. Other errors will be logged as normal.
        let mut log_connection_attempt = true;

        loop {
            if log_connection_attempt {
                info!("AutoClient for {:?} connecting", self.auto_url);
            }

            match self.connect().await {
                Ok(sse_client) => {
                    info!("AutoClient for {:?} connected", self.auto_url);

                    // Log any future connection errors.
                    log_connection_attempt = true;

                    match self.handle_sse(sse_client).await {
                        HandleSseEndState::Abort => {
                            return;
                        }
                        HandleSseEndState::Reconnect => (),
                    }
                }
                Err(http_sse::ClientConnectError::MakeRequest(e)) if e.is_connect() => {
                    if log_connection_attempt {
                        log_connection_attempt = false;

                        tracing::error!(
                            "AutoClient for {:?} error connecting: {:#}",
                            self.auto_url,
                            anyhow!(e)
                        );
                    }
                }
                Err(e) => {
                    tracing::error!(
                        "AutoClient for {:?} making request: {:#}",
                        self.auto_url,
                        anyhow!(e)
                    );
                }
            }

            Self::wait_before_reconnecting().await;
        }
    }

    async fn connect(&self) -> Result<http_sse::Client, http_sse::ClientConnectError> {
        // The /auto protocol has no heartbeat, so, without TCP keepalive, a client cannot
        // differentiate a repository that is not updating from a repository that has dropped
        // the connection.
        match http_sse::Client::connect(
            fuchsia_hyper::new_https_client_from_tcp_options(
                fuchsia_hyper::TcpOptions::keepalive_timeout(TCP_KEEPALIVE_TIMEOUT),
            ),
            &self.auto_url,
        )
        .await
        {
            Ok(sse_client) => {
                self.inspect.connect_success_count.increment();
                Ok(sse_client)
            }
            Err(e) => {
                self.inspect.connect_failure_count.increment();
                Err(e)
            }
        }
    }

    async fn handle_sse(&self, mut sse_event_stream: http_sse::Client) -> HandleSseEndState {
        while let Some(item) = sse_event_stream.next().await {
            match item {
                Ok(event) => {
                    if event.event_type() != "timestamp.json" {
                        tracing::error!(
                            "AutoClient for {:?} ignoring unrecognized event: {:?}",
                            self.auto_url,
                            event
                        );
                        continue;
                    }
                    info!("AutoClient for {:?} observed valid event: {:?}", self.auto_url, event);
                    if let Some(updating_client) = self.updating_client.upgrade() {
                        self.inspect.update_attempt_count.increment();
                        if let Err(e) = updating_client.lock().await.update().await {
                            tracing::error!(
                                "AutoClient for {:?} error updating TUF client: {:#}",
                                self.auto_url,
                                anyhow!(e)
                            );
                        }
                    } else {
                        return HandleSseEndState::Abort;
                    }
                }
                Err(e) => {
                    tracing::error!(
                        "AutoClient for {:?} event stream read error: {:#}",
                        self.auto_url,
                        anyhow!(e)
                    );
                    return HandleSseEndState::Reconnect;
                }
            }
        }
        tracing::error!("AutoClient for {:?} event stream closed.", self.auto_url);
        HandleSseEndState::Reconnect
    }

    async fn wait_before_reconnecting() {
        fasync::Timer::new(fasync::Time::after(AUTO_CLIENT_SSE_RECONNECT_DELAY)).await
    }
}

impl Drop for AutoClient {
    fn drop(&mut self) {
        info!("AutoClient for {:?} stopping.", self.auto_url);
    }
}

enum HandleSseEndState {
    Reconnect,
    Abort,
}
