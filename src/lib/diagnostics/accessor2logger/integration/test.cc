// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/cpp/fidl.h>
#include <fuchsia/logger/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/log_settings.h>
#include <lib/syslog/cpp/macros.h>

#include <algorithm>
#include <iterator>

#include <gtest/gtest.h>
#include <src/lib/diagnostics/accessor2logger/log_message.h>

const char* TAG = "integration_test";

void WriteLogs() {
  syslog::SetTags({TAG});
  // WARNING: Test is sensitive to line numbers and file name.
  // These log lines are added to the top of the file to prevent changes to the test below from
  // changing the expected outputs.
  FX_LOGS(INFO) << "Hello info";        // Line 24
  FX_LOGS(WARNING) << "Hello warning";  // Line 25
  FX_LOGS(ERROR) << "Hello error";      // Line 26
}

std::vector<fuchsia::logger::LogMessage> GetLogs() {
  // Synchronously get the logs from ArchiveAccessor.

  fuchsia::diagnostics::ArchiveAccessorSyncPtr accessor;
  auto services = sys::ServiceDirectory::CreateFromNamespace();
  services->Connect(accessor.NewRequest());
  fuchsia::diagnostics::BatchIteratorSyncPtr iterator;

  auto params = fuchsia::diagnostics::StreamParameters();
  params.set_stream_mode(fuchsia::diagnostics::StreamMode::SNAPSHOT);
  params.set_data_type(fuchsia::diagnostics::DataType::LOGS);
  params.set_format(fuchsia::diagnostics::Format::JSON);
  params.set_client_selector_configuration(
      fuchsia::diagnostics::ClientSelectorConfiguration::WithSelectAll(true));

  EXPECT_EQ(ZX_OK, accessor->StreamDiagnostics(std::move(params), iterator.NewRequest()));

  fuchsia::diagnostics::BatchIterator_GetNext_Result result;
  std::vector<fuchsia::logger::LogMessage> ret;
  EXPECT_EQ(ZX_OK, iterator->GetNext(&result));
  for (auto& content : result.response().batch) {
    auto result =
        diagnostics::accessor2logger::ConvertFormattedContentToLogMessages(std::move(content));
    if (result.is_error()) {
      EXPECT_TRUE(false) << "Received an error: " << result.error();
      return {};
    }

    for (auto& value : result.take_value()) {
      if (value.is_error()) {
        EXPECT_TRUE(false) << "Value had an error: " << value.error();
        return {};
      }
      ret.emplace_back(value.take_value());
    }
  }

  return ret;
}

const size_t EXPECTED_LOGS = 3;

TEST(Accessor2Logger, ConversionWorks) {
  WriteLogs();
  auto logs = GetLogs();
  while (logs.size() != EXPECTED_LOGS) {
    // Repeat with 1s wait if logs do not appear yet.
    // This can happen when the test Archivist has not yet finished reading the logs off of the
    // wire.
    sleep(1);
    printf("Retrying reading logs\n");
    logs = GetLogs();
  }

  ASSERT_EQ(logs.size(), EXPECTED_LOGS);

  EXPECT_GT(logs[0].time, 0u);
  EXPECT_GT(logs[0].pid, 0u);
  EXPECT_GT(logs[0].tid, 0u);
  EXPECT_EQ(logs[0].msg, "[test.cc(24)] Hello info");
  ASSERT_EQ(logs[0].tags.size(), 1u);
  EXPECT_EQ(logs[0].tags[0], TAG);
  EXPECT_EQ(logs[0].severity, static_cast<int8_t>(fuchsia::logger::LogLevelFilter::INFO));

  EXPECT_GT(logs[1].time, 0u);
  EXPECT_GT(logs[1].pid, 0u);
  EXPECT_GT(logs[1].tid, 0u);
  EXPECT_EQ(logs[1].msg,
            "[src/lib/diagnostics/accessor2logger/integration/test.cc(25)] Hello warning");
  ASSERT_EQ(logs[1].tags.size(), 1u);
  EXPECT_EQ(logs[1].tags[0], TAG);
  EXPECT_EQ(logs[1].severity, static_cast<int8_t>(fuchsia::logger::LogLevelFilter::WARN));

  EXPECT_GT(logs[2].time, 0u);
  EXPECT_GT(logs[2].pid, 0u);
  EXPECT_GT(logs[2].tid, 0u);
  EXPECT_EQ(logs[2].msg,
            "[src/lib/diagnostics/accessor2logger/integration/test.cc(26)] Hello error");
  ASSERT_EQ(logs[2].tags.size(), 1u);
  EXPECT_EQ(logs[2].tags[0], TAG);
  EXPECT_EQ(logs[2].severity, static_cast<int8_t>(fuchsia::logger::LogLevelFilter::ERROR));
}
