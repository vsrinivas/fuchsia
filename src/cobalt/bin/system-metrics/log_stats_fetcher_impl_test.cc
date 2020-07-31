// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/log_stats_fetcher_impl.h"

#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/test_loop_fixture.h>

#include "src/cobalt/bin/system-metrics/testing/fake_archive.h"

namespace cobalt {
namespace {

const std::unordered_map<std::string, ComponentEventCode> kComponentCodeMap = {
    {"fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm", ComponentEventCode::Appmgr},
    {"fuchsia-pkg://fuchsia.com/sysmgr#meta/sysmgr.cmx", ComponentEventCode::Sysmgr}};

const char kBaselineArchive[] = R"(
{
  "moniker": "core/archivist",
  "payload": {
    "root": {
      "log_stats": {
        "error_logs": 8,
        "kernel_logs": 5,
        "by_component": {
           "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm": {
             "error_logs": 4
           }
        }
      }
    }
  }
}
)";

class LogStatsFetcherImplTest : public gtest::TestLoopFixture {
 public:
  LogStatsFetcherImplTest()
      : fake_archive_(kBaselineArchive),
        fetcher_(
            dispatcher(), [this] { return BindArchiveAccessor(); }, kComponentCodeMap) {
    RunLoopUntilIdle();
  }

  bool FetchMetricsSync(LogStatsFetcher::Metrics* result) {
    bool fetched = false;
    fetcher_.FetchMetrics([&fetched, result](const LogStatsFetcher::Metrics& metrics) {
      *result = metrics;
      fetched = true;
    });
    RunLoopUntilIdle();
    return fetched;
  }

  void ReplaceArchive(const char* archive) { fake_archive_ = FakeArchive(archive); }

 protected:
  fuchsia::diagnostics::ArchiveAccessorPtr BindArchiveAccessor() {
    fuchsia::diagnostics::ArchiveAccessorPtr ret;
    bindings_.AddBinding(&fake_archive_, ret.NewRequest());
    return ret;
  }

  fidl::BindingSet<fuchsia::diagnostics::ArchiveAccessor> bindings_;
  FakeArchive fake_archive_;
  LogStatsFetcherImpl fetcher_;
};

TEST_F(LogStatsFetcherImplTest, Simple) {
  const char archive[] = R"(
{
  "moniker": "core/archivist",
  "payload": {
    "root": {
      "log_stats": {
        "error_logs": 11,
        "kernel_logs": 6,
        "by_component": {
           "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm": {
             "error_logs": 6
           }
        }
      }
    }
  }
}
)";
  ReplaceArchive(archive);

  LogStatsFetcher::Metrics metrics;
  ASSERT_TRUE(FetchMetricsSync(&metrics));
  // All counts are relative to the baseline, e.g. error count = 11-8 = 3.
  EXPECT_EQ(3u, metrics.error_count);
  EXPECT_EQ(1u, metrics.klog_count);
  EXPECT_EQ(1u, metrics.per_component_error_count.size());
  EXPECT_EQ(2u, metrics.per_component_error_count[ComponentEventCode::Appmgr]);
}

TEST_F(LogStatsFetcherImplTest, CountDrops) {
  const char archive[] = R"(
{
  "moniker": "core/archivist",
  "payload": {
    "root": {
      "log_stats": {
        "error_logs": 4,
        "kernel_logs": 2,
        "by_component": {
           "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm": {
             "error_logs": 1
           }
        }
      }
    }
  }
}
)";
  ReplaceArchive(archive);

  LogStatsFetcher::Metrics metrics;
  ASSERT_TRUE(FetchMetricsSync(&metrics));
  // We should not subtract the baseline if the counts drop (it is assumed that the components have
  // restarted).
  EXPECT_EQ(4u, metrics.error_count);
  EXPECT_EQ(2u, metrics.klog_count);
  EXPECT_EQ(1u, metrics.per_component_error_count.size());
  EXPECT_EQ(1u, metrics.per_component_error_count[ComponentEventCode::Appmgr]);
}

TEST_F(LogStatsFetcherImplTest, MultipleComponents) {
  const char archive[] = R"(
{
  "moniker": "core/archivist",
  "payload": {
    "root": {
      "log_stats": {
        "error_logs": 12,
        "kernel_logs": 6,
        "by_component": {
           "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm": {
             "error_logs": 6
           },
           "fuchsia-pkg://fuchsia.com/sysmgr#meta/sysmgr.cmx": {
             "error_logs": 1
           }
        }
      }
    }
  }
}
)";
  ReplaceArchive(archive);

  LogStatsFetcher::Metrics metrics;
  ASSERT_TRUE(FetchMetricsSync(&metrics));
  EXPECT_EQ(4u, metrics.error_count);
  EXPECT_EQ(1u, metrics.klog_count);
  EXPECT_EQ(2u, metrics.per_component_error_count.size());
  EXPECT_EQ(2u, metrics.per_component_error_count[ComponentEventCode::Appmgr]);
  EXPECT_EQ(1u, metrics.per_component_error_count[ComponentEventCode::Sysmgr]);
}

TEST_F(LogStatsFetcherImplTest, NotInAllowlist) {
  const char archive[] = R"(
{
  "moniker": "core/archivist",
  "payload": {
    "root": {
      "log_stats": {
        "error_logs": 11,
        "kernel_logs": 6,
        "by_component": {
           "fuchsia-pkg://fuchsia.com/appmgr#meta/appmgr.cm": {
             "error_logs": 6
           },
           "fuchsia-pkg://fuchsia.com/foo#meta/bar.cmx": {
             "error_logs": 1
           }
        }
      }
    }
  }
}
)";
  ReplaceArchive(archive);

  LogStatsFetcher::Metrics metrics;
  ASSERT_TRUE(FetchMetricsSync(&metrics));
  EXPECT_EQ(3u, metrics.error_count);
  EXPECT_EQ(1u, metrics.klog_count);
  // Since foo is not in the allowlist, it should not have a corresponding entry in the map.
  EXPECT_EQ(1u, metrics.per_component_error_count.size());
  EXPECT_EQ(2u, metrics.per_component_error_count[ComponentEventCode::Appmgr]);
}

}  // namespace
}  // namespace cobalt
