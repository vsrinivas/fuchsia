// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Serve 64 schemas at a time.
/// We limit to 64 because each schema is sent over a VMO and we can only have
/// 64 handles sent over a message.
// TODO(fxbug.dev/4601): Greedily fill the vmos with object delimited json, rather than
// giving every schema its own vmo.
pub const IN_MEMORY_SNAPSHOT_LIMIT: usize = 64;

// Number of seconds to wait for a single component to have its diagnostics data "pumped".
// This involves diagnostics directory traversal, contents extraction, and snapshotting.
pub const PER_COMPONENT_ASYNC_TIMEOUT_SECONDS: i64 = 10;

/// Name used by clients to connect to the feedback diagnostics protocol.
/// This protocol applies static selectors configured under config/data/feedback to
/// inspect exfiltration.
pub const FEEDBACK_ARCHIVE_ACCESSOR_NAME: &str = "fuchsia.diagnostics.FeedbackArchiveAccessor";

/// Name used by clients to connect to the legacy metrics diagnostics protocol.
/// This protocol applies static selectors configured under
/// config/data/legacy_metrics to inspect exfiltration.
pub const LEGACY_METRICS_ARCHIVE_ACCESSOR_NAME: &str =
    "fuchsia.diagnostics.LegacyMetricsArchiveAccessor";

/// The maximum number of Inspect files that can be simultaneously snapshotted and formatted per
/// reader.
pub const MAXIMUM_SIMULTANEOUS_SNAPSHOTS_PER_READER: usize = 4;

/// The maximum number of bytes in a formatted content VMO.
pub const FORMATTED_CONTENT_CHUNK_SIZE_TARGET: u64 = 1 << 20; // 1 MiB

/// For production archivist instances this value is retrieved from configuration but we still
/// provide a default here for internal testing purposes.
#[cfg(test)]
pub(crate) const LEGACY_DEFAULT_MAXIMUM_CACHED_LOGS_BYTES: usize = 4 * 1024 * 1024;

/// The root Archivist's moniker in the component topology, used for attributing our own logs.
// TODO(fxbug.dev/50105,fxbug.dev/64197): update this to reflect updated monikers received in events
pub const ARCHIVIST_MONIKER: &str = "./archivist:0";

/// The root Archivist's URL in bootfs, used for attributing our own logs.
pub const ARCHIVIST_URL: &str = "fuchsia-boot:///archivist.cm";

/// Default path where pipeline configuration are located.
pub const DEFAULT_PIPELINES_PATH: &str = "/config/data";
