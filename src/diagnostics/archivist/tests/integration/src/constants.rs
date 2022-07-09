// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

pub const ARCHIVIST_WITH_FEEDBACK_FILTERING: &str = "#meta/archivist-with-feedback-filtering.cm";
pub const ARCHIVIST_WITH_LOWPAN_FILTERING: &str = "#meta/archivist-with-lowpan-filtering.cm";
pub const ARCHIVIST_WITH_FEEDBACK_FILTERING_DISABLED: &str =
    "#meta/archivist-with-feedback-filtering-disabled.cm";
pub const ARCHIVIST_WITH_SMALL_CACHES: &str = "#meta/archivist-with-small-caches.cm";
pub const ARCHIVIST_WITH_LEGACY_METRICS: &str = "#meta/archivist-with-legacy-metrics-filtering.cm";
pub const INTEGRATION_ARCHIVIST_URL: &str = "#meta/archivist.cm";
pub const ARCHIVIST_WITH_KLOG_URL: &str = "#meta/archivist_with_klog.cm";
pub const ARCHIVIST_FOR_V1_URL: &str = "#meta/archivist_for_v1.cm";
pub const COMPONENT_WITH_CHILDREN_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests#meta/component_with_children.cm";
pub const IQUERY_TEST_COMPONENT_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests#meta/test_component.cm";
pub const LOG_AND_CRASH_COMPONENT_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests#meta/log-and-crash.cm";
pub const SOCKET_PUPPET_COMPONENT_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests#meta/socket-puppet.cm";
pub const LOG_AND_EXIT_COMPONENT_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests#meta/log-and-exit.cm";
pub const LOGGING_COMPONENT_URL: &str = "#meta/logging_component.cm";
pub const STUB_INSPECT_COMPONENT_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests#meta/stub_inspect_component.cm";
pub const HANGING_INSPECT_COMPONENT_URL: &str = "#meta/hanging_inspect_component.cm";
pub const LOGGER_COMPONENT_FOR_INTEREST_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests#meta/log-on-interest.cm";
pub const STDIO_PUPPET_URL: &str =
    "fuchsia-pkg://fuchsia.com/archivist-integration-tests#meta/stdio-puppet.cm";
