// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/cobalt/bin/system-metrics/archivist_stats_fetcher_impl.h"

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/gtest/test_loop_fixture.h>

#include <set>

#include <gtest/gtest.h>
#include <src/lib/fsl/vmo/strings.h>

namespace {

const uint64_t kExpectedMetricCount = 11;

const char kExpectedArchiveOutput[] = R"(
{
  "moniker": "archivist.cmx",
  "payload": {
    "root": {
      "all_archive_accessor_node": {
        "archive_accessor_connections_closed": 1,
        "archive_accessor_connections_opened": 2,
        "inspect_batch_iterator_connections_closed": 3,
        "inspect_batch_iterator_connections_opened": 4,
        "inspect_batch_iterator_get_next_errors": 5,
        "inspect_batch_iterator_get_next_requests": 6,
        "inspect_batch_iterator_get_next_responses": 7,
        "inspect_batch_iterator_get_next_result_count": 8,
        "inspect_batch_iterator_get_next_result_errors": 9,
        "inspect_component_timeouts_count": 10,
        "inspect_reader_servers_constructed": 11,
        "inspect_reader_servers_destroyed": 12,
        "lifecycle_batch_iterator_connections_closed": 13,
        "lifecycle_batch_iterator_connections_opened": 14,
        "lifecycle_batch_iterator_get_next_errors": 15,
        "lifecycle_batch_iterator_get_next_requests": 16,
        "lifecycle_batch_iterator_get_next_responses": 17,
        "lifecycle_batch_iterator_get_next_result_count": 18,
        "lifecycle_batch_iterator_get_next_result_errors": 19,
        "lifecycle_component_timeouts_count": 20,
        "lifecycle_reader_servers_constructed": 21,
        "lifecycle_reader_servers_destroyed": 22,
        "stream_diagnostics_requests": 23
      }
    }
  }
}
)";

// A faked implementation of ArchiveAccessor that repeatedly returns only one component with the
// given JSON value.
class FakeArchive : public fuchsia::diagnostics::ArchiveAccessor {
 public:
  explicit FakeArchive(std::string return_value)
      : iterator_bindings_(std::make_unique<fidl::BindingSet<fuchsia::diagnostics::BatchIterator,
                                                             std::unique_ptr<FakeIterator>>>()),
        return_value_(std::move(return_value)) {}

  FakeArchive(FakeArchive&&) = default;
  FakeArchive(const FakeArchive&) = delete;
  FakeArchive& operator=(FakeArchive&&) = default;
  FakeArchive& operator=(const FakeArchive&) = delete;

  void StreamDiagnostics(
      fuchsia::diagnostics::StreamParameters params,
      fidl::InterfaceRequest<fuchsia::diagnostics::BatchIterator> request) override {
    iterator_bindings_->AddBinding(std::make_unique<FakeIterator>(return_value_),
                                   std::move(request));
  }

 private:
  class FakeIterator : public fuchsia::diagnostics::BatchIterator {
   public:
    explicit FakeIterator(std::string return_value) : return_value_(std::move(return_value)) {}

   private:
    void GetNext(GetNextCallback callback) override {
      std::vector<fuchsia::diagnostics::FormattedContent> contents;
      if (!sent) {
        fuchsia::diagnostics::FormattedContent content;
        ZX_ASSERT(fsl::VmoFromString(return_value_, &content.json()));
        contents.emplace_back(std::move(content));
        sent = true;
      }
      callback(fit::ok(std::move(contents)));
    }

    bool sent = false;
    std::string return_value_;
  };
  std::unique_ptr<
      fidl::BindingSet<fuchsia::diagnostics::BatchIterator, std::unique_ptr<FakeIterator>>>
      iterator_bindings_;
  std::string return_value_;
};

class TestArchivistStatsFetcherImpl : public cobalt::ArchivistStatsFetcherImpl {
 public:
  TestArchivistStatsFetcherImpl(async_dispatcher_t* dispatcher,
                                fit::function<fuchsia::diagnostics::ArchiveAccessorPtr()> callback)
      : ArchivistStatsFetcherImpl(dispatcher, std::move(callback)) {}
};

class ArchivistStatsFetcherImplTest : public gtest::TestLoopFixture {
 public:
  ArchivistStatsFetcherImplTest()
      : fake_archive_(kExpectedArchiveOutput),
        test_fetcher_(
            TestArchivistStatsFetcherImpl(dispatcher(), [this] { return BindArchive(); })) {}

 protected:
  fuchsia::diagnostics::ArchiveAccessorPtr BindArchive() {
    fuchsia::diagnostics::ArchiveAccessorPtr ret;
    archive_bindings_.AddBinding(&fake_archive_, ret.NewRequest());
    return ret;
  }

  FakeArchive fake_archive_;
  TestArchivistStatsFetcherImpl test_fetcher_;
  fidl::BindingSet<fuchsia::diagnostics::ArchiveAccessor> archive_bindings_;
};

// Ensure that the expected number of measurements are uploaded and that they do not get reuploaded
// if they did not change.
TEST_F(ArchivistStatsFetcherImplTest, MeasurementsDoNotRepeatSuccess) {
  size_t count = 0;
  test_fetcher_.FetchMetrics([&](cobalt::ArchivistStatsFetcher::Measurement measurement) {
    count++;
    return true;
  });
  RunLoopUntilIdle();
  EXPECT_EQ(kExpectedMetricCount, count);

  // Try loading again, since no metrics changed they will not be uploaded again.
  count = 0;
  test_fetcher_.FetchMetrics([&](cobalt::ArchivistStatsFetcher::Measurement measurement) {
    count++;
    return true;
  });
  EXPECT_EQ(0u, count);
}

// Ensure that all measurements are reuploaded if they fail.
TEST_F(ArchivistStatsFetcherImplTest, MeasurementsRepeatFailed) {
  size_t count = 0;
  test_fetcher_.FetchMetrics([&](cobalt::ArchivistStatsFetcher::Measurement measurement) {
    count++;
    return false;
  });
  RunLoopUntilIdle();
  EXPECT_EQ(kExpectedMetricCount, count);

  // Try fetching again. Since all metrics failed, they will all show up.
  count = 0;
  test_fetcher_.FetchMetrics([&](cobalt::ArchivistStatsFetcher::Measurement measurement) {
    count++;
    return true;
  });
  RunLoopUntilIdle();
  EXPECT_EQ(kExpectedMetricCount, count);
}

// Ensure that updating multiple metrics over time works as expected.
TEST_F(ArchivistStatsFetcherImplTest, MetricsUpdatedOverTime) {
  std::vector<uint64_t> values;
  fake_archive_ = FakeArchive(R"(
    {"moniker": "archivist.cmx",
     "payload": {"root": {"all_archive_accessor_node": {
       "archive_accessor_connections_opened": 5
     }}}})");
  test_fetcher_.FetchMetrics([&](cobalt::ArchivistStatsFetcher::Measurement measurement) {
    values.push_back(measurement.second);
    return true;
  });
  RunLoopUntilIdle();
  ASSERT_EQ(1u, values.size());
  EXPECT_EQ(5u, values[0]);

  values.clear();
  fake_archive_ = FakeArchive(R"(
    {"moniker": "archivist.cmx",
     "payload": {"root": {"all_archive_accessor_node": {
       "archive_accessor_connections_opened": 7
     }}}})");
  test_fetcher_.FetchMetrics([&](cobalt::ArchivistStatsFetcher::Measurement measurement) {
    values.push_back(measurement.second);
    return true;
  });
  RunLoopUntilIdle();
  ASSERT_EQ(1u, values.size());
  EXPECT_EQ(2u, values[0]);

  values.clear();
  fake_archive_ = FakeArchive(R"(
    {"moniker": "archivist.cmx",
     "payload": {"root": {"all_archive_accessor_node": {
       "inspect_batch_iterator_get_next_errors": 10 
     }}}})");
  test_fetcher_.FetchMetrics([&](cobalt::ArchivistStatsFetcher::Measurement measurement) {
    values.push_back(measurement.second);
    return true;
  });
  RunLoopUntilIdle();
  ASSERT_EQ(1u, values.size());
  EXPECT_EQ(10u, values[0]);

  values.clear();
  fake_archive_ = FakeArchive(R"(
    {"moniker": "archivist.cmx",
     "payload": {"root": {"all_archive_accessor_node": {
       "archive_accessor_connections_opened": 8,
       "inspect_batch_iterator_get_next_errors": 10 
     }}}})");
  test_fetcher_.FetchMetrics([&](cobalt::ArchivistStatsFetcher::Measurement measurement) {
    values.push_back(measurement.second);
    return true;
  });
  RunLoopUntilIdle();
  ASSERT_EQ(1u, values.size());
  EXPECT_EQ(1u, values[0]);

  values.clear();
  fake_archive_ = FakeArchive(R"(
    {"moniker": "archivist.cmx",
     "payload": {"root": {"all_archive_accessor_node": {
       "archive_accessor_connections_opened": 9,
       "inspect_batch_iterator_get_next_errors": 11 
     }}}})");
  test_fetcher_.FetchMetrics([&](cobalt::ArchivistStatsFetcher::Measurement measurement) {
    values.push_back(measurement.second);
    return true;
  });
  RunLoopUntilIdle();
  ASSERT_EQ(2u, values.size());
  EXPECT_EQ(1u, values[0]);
  EXPECT_EQ(1u, values[1]);
}
}  // namespace
