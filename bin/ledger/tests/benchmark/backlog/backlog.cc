// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/tests/benchmark/backlog/backlog.h"

#include <iostream>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/callback/waiter.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/strings/string_number_conversions.h>
#include <trace/event.h>
#include "lib/fidl/cpp/clone.h"

#include "peridot/bin/ledger/filesystem/get_directory_content_size.h"
#include "peridot/bin/ledger/testing/get_ledger.h"
#include "peridot/bin/ledger/testing/quit_on_error.h"
#include "peridot/bin/ledger/testing/run_with_tracing.h"
#include "peridot/lib/convert/convert.h"

namespace {
constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/backlog";
constexpr fxl::StringView kUniqueKeyCountFlag = "unique-key-count";
constexpr fxl::StringView kValueSizeFlag = "value-size";
constexpr fxl::StringView kCommitCountFlag = "commit-count";
constexpr fxl::StringView kRefsFlag = "refs";
constexpr fxl::StringView kServerIdFlag = "server-id";

constexpr fxl::StringView kRefsOnFlag = "on";
constexpr fxl::StringView kRefsOffFlag = "off";

constexpr size_t kKeySize = 100;
const std::string kUserDirectory = "/backlog_user";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: "
            << executable_name
            // Comment to make clang format not break formatting.
            << " --" << kUniqueKeyCountFlag << "=<int>"
            << " --" << kValueSizeFlag << "=<int>"
            << " --" << kCommitCountFlag << "=<int>"
            << " --" << kRefsFlag << "=(" << kRefsOnFlag << "|" << kRefsOffFlag
            << ")"
            << " --" << kServerIdFlag << "=<string>" << std::endl;
}

}  // namespace

namespace test {
namespace benchmark {

BacklogBenchmark::BacklogBenchmark(
    async::Loop* loop, size_t unique_key_count, size_t value_size,
    size_t commit_count,
    PageDataGenerator::ReferenceStrategy reference_strategy,
    std::string server_id)
    : loop_(loop),
      startup_context_(fuchsia::sys::StartupContext::CreateFromStartupInfo()),
      cloud_provider_firebase_factory_(startup_context_.get()),
      sync_watcher_binding_(this),
      unique_key_count_(unique_key_count),
      value_size_(value_size),
      commit_count_(commit_count),
      reference_strategy_(reference_strategy),
      server_id_(std::move(server_id)),
      writer_tmp_dir_(kStoragePath),
      reader_tmp_dir_(kStoragePath) {
  FXL_DCHECK(loop_);
  FXL_DCHECK(unique_key_count_ > 0);
  FXL_DCHECK(value_size_ > 0);
  FXL_DCHECK(commit_count_ > 0);
  cloud_provider_firebase_factory_.Init();
}

void BacklogBenchmark::SyncStateChanged(ledger::SyncState download,
                                        ledger::SyncState upload,
                                        SyncStateChangedCallback callback) {
  if (on_sync_state_changed_) {
    on_sync_state_changed_(download, upload);
  }
  callback();
}

void BacklogBenchmark::Run() { ConnectWriter(); }

void BacklogBenchmark::ConnectWriter() {
  // Name of the storage directory currently identifies the user. Ensure the
  // most nested directory has the same name to make the ledgers sync.
  std::string writer_path = writer_tmp_dir_.path() + kUserDirectory;
  FXL_DCHECK(files::CreateDirectory(writer_path));

  test::GetLedger(
      startup_context_.get(), writer_controller_.NewRequest(), nullptr,
      "backlog", writer_path, []() { FXL_LOG(INFO) << "Writer closed."; },
      [this](ledger::Status status, ledger::LedgerPtr writer) {
        if (QuitOnError(QuitLoopClosure(), status, "Get writer ledger")) {
          return;
        }
        writer_ = std::move(writer);

        test::GetPageEnsureInitialized(
            &writer_, nullptr, []() { FXL_LOG(INFO) << "Writer page closed."; },
            [this](ledger::Status status, ledger::PagePtr writer_page,
                   ledger::PageId page_id) {
              if (QuitOnError(QuitLoopClosure(), status,
                              "Writer page initialization")) {
                return;
              }

              writer_page_ = std::move(writer_page);
              page_id_ = page_id;

              TRACE_ASYNC_BEGIN("benchmark", "populate", 0);
              Populate();
            });
      });
}

void BacklogBenchmark::Populate() {
  int transaction_size = static_cast<int>(
      ceil(static_cast<double>(unique_key_count_) / commit_count_));
  int key_count = std::max(unique_key_count_, commit_count_);
  FXL_LOG(INFO) << "Transaction size: " << transaction_size
                << ", key count: " << key_count << ".";
  auto keys = generator_.MakeKeys(key_count, kKeySize, unique_key_count_);
  page_data_generator_.Populate(
      &writer_page_, std::move(keys), value_size_, transaction_size,
      reference_strategy_, ledger::Priority::EAGER,
      [this](ledger::Status status) {
        if (status != ledger::Status::OK) {
          if (QuitOnError(QuitLoopClosure(), status,
                          "PageGenerator::Populate")) {
            return;
          }
          return;
        }
        TRACE_ASYNC_END("benchmark", "populate", 0);
        DisconnectAndRecordWriter();
      });
}

void BacklogBenchmark::DisconnectAndRecordWriter() {
  test::KillLedgerProcess(&writer_controller_);
  RecordDirectorySize("writer_directory_size", writer_tmp_dir_.path());
  ConnectUploader();
}

void BacklogBenchmark::ConnectUploader() {
  // Name of the storage directory currently identifies the user. Ensure the
  // most nested directory has the same name to make the ledgers sync.
  std::string uploader_path = writer_tmp_dir_.path() + kUserDirectory;

  cloud_provider::CloudProviderPtr cloud_provider_uploader;
  cloud_provider_firebase_factory_.MakeCloudProvider(
      server_id_, "backlog", cloud_provider_uploader.NewRequest());
  test::GetLedger(
      startup_context_.get(), uploader_controller_.NewRequest(),
      std::move(cloud_provider_uploader), "backlog", uploader_path,
      QuitLoopClosure(),
      [this](ledger::Status status, ledger::LedgerPtr uploader) {
        if (QuitOnError(QuitLoopClosure(), status, "Get uploader ledger")) {
          return;
        }
        uploader_ = std::move(uploader);

        TRACE_ASYNC_BEGIN("benchmark", "get_uploader_page", 0);
        TRACE_ASYNC_BEGIN("benchmark", "upload", 0);
        uploader_->GetPage(
            fidl::MakeOptional(fidl::Clone(page_id_)),
            uploader_page_.NewRequest(), [this](ledger::Status status) {
              if (QuitOnError(QuitLoopClosure(), status, "GetPage")) {
                return;
              }
              TRACE_ASYNC_END("benchmark", "get_uploader_page", 0);
              WaitForUploaderUpload();
            });
      });
}

void BacklogBenchmark::WaitForUploaderUpload() {
  on_sync_state_changed_ = [this](ledger::SyncState download,
                                  ledger::SyncState upload) {
    if (upload == ledger::SyncState::IDLE) {
      on_sync_state_changed_ = nullptr;
      TRACE_ASYNC_END("benchmark", "upload", 0);
      // Stop watching sync state for this page.
      sync_watcher_binding_.Unbind();
      ConnectReader();
      return;
    }
  };
  uploader_page_->SetSyncStateWatcher(
      sync_watcher_binding_.NewBinding(),
      QuitOnErrorCallback(QuitLoopClosure(), "Page::SetSyncStateWatcher"));
}

void BacklogBenchmark::ConnectReader() {
  std::string reader_path = reader_tmp_dir_.path() + kUserDirectory;
  FXL_DCHECK(files::CreateDirectory(reader_path));

  cloud_provider::CloudProviderPtr cloud_provider_reader;
  cloud_provider_firebase_factory_.MakeCloudProvider(
      server_id_, "backlog", cloud_provider_reader.NewRequest());
  test::GetLedger(
      startup_context_.get(), reader_controller_.NewRequest(),
      std::move(cloud_provider_reader), "backlog", reader_path,
      QuitLoopClosure(),
      [this](ledger::Status status, ledger::LedgerPtr reader) {
        if (QuitOnError(QuitLoopClosure(), status, "ConnectReader")) {
          return;
        }
        reader_ = std::move(reader);

        TRACE_ASYNC_BEGIN("benchmark", "download", 0);
        TRACE_ASYNC_BEGIN("benchmark", "get_reader_page", 0);
        reader_->GetPage(
            fidl::MakeOptional(page_id_), reader_page_.NewRequest(),
            [this](ledger::Status status) {
              if (QuitOnError(QuitLoopClosure(), status, "GetPage")) {
                return;
              }
              TRACE_ASYNC_END("benchmark", "get_reader_page", 0);
              WaitForReaderDownload();
            });
      });
}

void BacklogBenchmark::WaitForReaderDownload() {
  on_sync_state_changed_ = [this](ledger::SyncState download,
                                  ledger::SyncState upload) {
    if (download == ledger::SyncState::IDLE) {
      on_sync_state_changed_ = nullptr;
      TRACE_ASYNC_END("benchmark", "download", 0);
      GetReaderSnapshot();
      return;
    }
  };
  reader_page_->SetSyncStateWatcher(
      sync_watcher_binding_.NewBinding(),
      QuitOnErrorCallback(QuitLoopClosure(), "Page::SetSyncStateWatcher"));
}

void BacklogBenchmark::GetReaderSnapshot() {
  reader_page_->GetSnapshot(
      reader_snapshot_.NewRequest(), fidl::VectorPtr<uint8_t>::New(0), nullptr,
      QuitOnErrorCallback(QuitLoopClosure(), "GetSnapshot"));
  TRACE_ASYNC_BEGIN("benchmark", "get_all_entries", 0);
  GetEntriesStep(nullptr, unique_key_count_);
}

void BacklogBenchmark::CheckStatusAndGetMore(
    ledger::Status status, size_t entries_left,
    std::unique_ptr<ledger::Token> next_token) {
  if ((status != ledger::Status::OK) &&
      (status != ledger::Status::PARTIAL_RESULT)) {
    if (QuitOnError(QuitLoopClosure(), status, "PageSnapshot::GetEntries")) {
      return;
    }
  }

  if (status == ledger::Status::OK) {
    TRACE_ASYNC_END("benchmark", "get_all_entries", 0);
    FXL_DCHECK(entries_left == 0);
    FXL_DCHECK(!next_token);
    ShutDown();
    RecordDirectorySize("uploader_directory_size", writer_tmp_dir_.path());
    RecordDirectorySize("reader_directory_size", reader_tmp_dir_.path());
    return;
  }
  FXL_DCHECK(next_token);
  GetEntriesStep(std::move(next_token), entries_left);
}

void BacklogBenchmark::GetEntriesStep(std::unique_ptr<ledger::Token> token,
                                      size_t entries_left) {
  FXL_DCHECK(entries_left > 0);
  TRACE_ASYNC_BEGIN("benchmark", "get_entries_partial", entries_left);
  if (reference_strategy_ == PageDataGenerator::ReferenceStrategy::INLINE) {
    reader_snapshot_->GetEntriesInline(
        fidl::VectorPtr<uint8_t>::New(0), std::move(token),
        [this, entries_left](ledger::Status status, auto entries,
                             auto next_token) mutable {
          TRACE_ASYNC_END("benchmark", "get_entries_partial", entries_left);
          CheckStatusAndGetMore(status, entries_left - entries->size(),
                                std::move(next_token));
        });
  } else {
    reader_snapshot_->GetEntries(
        fidl::VectorPtr<uint8_t>::New(0), std::move(token),
        [this, entries_left](ledger::Status status, auto entries,
                             auto next_token) mutable {
          TRACE_ASYNC_END("benchmark", "get_entries_partial", entries_left);
          CheckStatusAndGetMore(status, entries_left - entries->size(),
                                std::move(next_token));
        });
  }
}

void BacklogBenchmark::RecordDirectorySize(const std::string& event_name,
                                           const std::string& path) {
  uint64_t tmp_dir_size = 0;
  FXL_CHECK(ledger::GetDirectoryContentSize(path, &tmp_dir_size));
  TRACE_COUNTER("benchmark", event_name.c_str(), 0, "directory_size",
                TA_UINT64(tmp_dir_size));
}

void BacklogBenchmark::ShutDown() {
  test::KillLedgerProcess(&uploader_controller_);
  test::KillLedgerProcess(&reader_controller_);
  loop_->Quit();
}

fit::closure BacklogBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

}  // namespace benchmark
}  // namespace test

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  std::string unique_key_count_str;
  size_t unique_key_count;
  std::string value_size_str;
  size_t value_size;
  std::string commit_count_str;
  size_t commit_count;
  std::string reference_strategy_str;
  std::string server_id;
  if (!command_line.GetOptionValue(kUniqueKeyCountFlag.ToString(),
                                   &unique_key_count_str) ||
      !fxl::StringToNumberWithError(unique_key_count_str, &unique_key_count) ||
      unique_key_count <= 0 ||
      !command_line.GetOptionValue(kValueSizeFlag.ToString(),
                                   &value_size_str) ||
      !fxl::StringToNumberWithError(value_size_str, &value_size) ||
      value_size <= 0 ||
      !command_line.GetOptionValue(kCommitCountFlag.ToString(),
                                   &commit_count_str) ||
      !fxl::StringToNumberWithError(commit_count_str, &commit_count) ||
      commit_count <= 0 ||
      !command_line.GetOptionValue(kRefsFlag.ToString(),
                                   &reference_strategy_str) ||
      !command_line.GetOptionValue(kServerIdFlag.ToString(), &server_id)) {
    PrintUsage(argv[0]);
    return -1;
  }

  test::benchmark::PageDataGenerator::ReferenceStrategy reference_strategy;
  if (reference_strategy_str == kRefsOnFlag) {
    reference_strategy =
        test::benchmark::PageDataGenerator::ReferenceStrategy::REFERENCE;
  } else if (reference_strategy_str == kRefsOffFlag) {
    reference_strategy =
        test::benchmark::PageDataGenerator::ReferenceStrategy::INLINE;
  } else {
    std::cerr << "Unknown option " << reference_strategy_str << " for "
              << kRefsFlag.ToString() << std::endl;
    PrintUsage(argv[0]);
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigMakeDefault);
  test::benchmark::BacklogBenchmark app(&loop, unique_key_count, value_size,
                                        commit_count, reference_strategy,
                                        server_id);
  return test::benchmark::RunWithTracing(&loop, [&app] { app.Run(); });
}
