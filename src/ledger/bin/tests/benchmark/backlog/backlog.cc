// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <iostream>
#include <memory>
#include <vector>

#include <trace/event.h>

#include "peridot/lib/rng/test_random.h"
#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/filesystem/get_directory_content_size.h"
#include "src/ledger/bin/testing/data_generator.h"
#include "src/ledger/bin/testing/get_ledger.h"
#include "src/ledger/bin/testing/get_page_ensure_initialized.h"
#include "src/ledger/bin/testing/page_data_generator.h"
#include "src/ledger/bin/testing/quit_on_error.h"
#include "src/ledger/bin/testing/run_with_tracing.h"
#include "src/ledger/bin/testing/sync_params.h"
#include "src/ledger/cloud_provider_firestore/bin/testing/cloud_provider_factory.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fsl/vmo/strings.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

namespace ledger {

namespace {
constexpr fxl::StringView kBinaryPath =
    "fuchsia-pkg://fuchsia.com/ledger_benchmarks#meta/backlog.cmx";
constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/backlog";
constexpr fxl::StringView kUniqueKeyCountFlag = "unique-key-count";
constexpr fxl::StringView kKeySizeFlag = "key-size";
constexpr fxl::StringView kValueSizeFlag = "value-size";
constexpr fxl::StringView kCommitCountFlag = "commit-count";
constexpr fxl::StringView kRefsFlag = "refs";
constexpr fxl::StringView kRefsOnFlag = "on";
constexpr fxl::StringView kRefsOffFlag = "off";

const std::string kUserDirectory = "/backlog_user";

void PrintUsage() {
  std::cout << "Usage: trace record "
            << kBinaryPath
            // Comment to make clang format not break formatting.
            << " --" << kUniqueKeyCountFlag << "=<int>"
            << " --" << kKeySizeFlag << "=<int>"
            << " --" << kValueSizeFlag << "=<int>"
            << " --" << kCommitCountFlag << "=<int>"
            << " --" << kRefsFlag << "=(" << kRefsOnFlag << "|" << kRefsOffFlag << ")"
            << GetSyncParamsUsage() << std::endl;
}

// Benchmark that measures time taken by a page connection to upload all local
// changes to the cloud; and for another connection to the same page to download
// all these changes.
//
// In contrast to the sync benchmark, backlog benchmark initiates the second
// connection only after the first one has uploaded all changes. It is designed
// to model the situation of adding new device instead of continuous
// synchronisation.
//
// Cloud sync needs to be configured on the device in order for the benchmark to
// run.
//
// Parameters:
//   --unique-key-count=<int> the number of unique keys to populate the page
//   with.
//   --key-size=<int> size of a key for each entry.
//   --value-size=<int> the size of values to populate the page with.
//   --commit-count=<int> the number of commits made to the page.
//   If this number is smaller than unique-key-count, changes will be bundled
//   into transactions. If it is bigger, some or all of the changes will use the
//   same keys, modifying the value.
//   --refs=(on|off) reference strategy: on to put values as references, off to
//     put them as FIDL arrays.
//   --credentials-path=<file path> Firestore service account credentials
class BacklogBenchmark : public SyncWatcher {
 public:
  BacklogBenchmark(async::Loop* loop, std::unique_ptr<sys::ComponentContext> component_context,
                   size_t unique_key_count, size_t key_size, size_t value_size, size_t commit_count,
                   PageDataGenerator::ReferenceStrategy reference_strategy, SyncParams sync_params);

  void Run();

  // SyncWatcher:
  void SyncStateChanged(SyncState download, SyncState upload,
                        SyncStateChangedCallback callback) override;

 private:
  void ConnectWriter();
  void Populate();
  void DisconnectAndRecordWriter();
  void ConnectUploader();
  void WaitForUploaderUpload();
  void ConnectReader();
  void WaitForReaderDownload();

  void GetReaderSnapshot();
  void GetEntriesStep(std::unique_ptr<Token> token, size_t entries_left);
  void CheckStatusAndGetMore(size_t entries_left, std::unique_ptr<Token> next_token);

  void RecordDirectorySize(const std::string& event_name, const std::string& path);
  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  rng::TestRandom random_;
  DataGenerator generator_;
  PageDataGenerator page_data_generator_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  cloud_provider_firestore::CloudProviderFactory cloud_provider_factory_;
  fidl::Binding<SyncWatcher> sync_watcher_binding_;
  const size_t unique_key_count_;
  const size_t key_size_;
  const size_t value_size_;
  const size_t commit_count_;
  const PageDataGenerator::ReferenceStrategy reference_strategy_;
  const cloud_provider_firestore::CloudProviderFactory::UserId user_id_;
  files::ScopedTempDir writer_tmp_dir_;
  files::ScopedTempDir reader_tmp_dir_;
  fuchsia::sys::ComponentControllerPtr writer_controller_;
  fuchsia::sys::ComponentControllerPtr uploader_controller_;
  fuchsia::sys::ComponentControllerPtr reader_controller_;
  LedgerPtr uploader_;
  LedgerPtr writer_;
  LedgerPtr reader_;
  PageId page_id_;
  PagePtr writer_page_;
  PagePtr uploader_page_;
  PagePtr reader_page_;
  PageSnapshotPtr reader_snapshot_;
  fit::function<void(SyncState, SyncState)> on_sync_state_changed_;

  FXL_DISALLOW_COPY_AND_ASSIGN(BacklogBenchmark);
};

BacklogBenchmark::BacklogBenchmark(async::Loop* loop,
                                   std::unique_ptr<sys::ComponentContext> component_context,
                                   size_t unique_key_count, size_t key_size, size_t value_size,
                                   size_t commit_count,
                                   PageDataGenerator::ReferenceStrategy reference_strategy,
                                   SyncParams sync_params)
    : loop_(loop),
      random_(0),
      generator_(&random_),
      page_data_generator_(&random_),
      component_context_(std::move(component_context)),
      cloud_provider_factory_(component_context_.get(), &random_, std::move(sync_params.api_key),
                              std::move(sync_params.credentials)),
      sync_watcher_binding_(this),
      unique_key_count_(unique_key_count),
      key_size_(key_size),
      value_size_(value_size),
      commit_count_(commit_count),
      reference_strategy_(reference_strategy),
      user_id_(cloud_provider_firestore::CloudProviderFactory::UserId::New()),
      writer_tmp_dir_(kStoragePath),
      reader_tmp_dir_(kStoragePath) {
  FXL_DCHECK(loop_);
  FXL_DCHECK(unique_key_count_ > 0);
  FXL_DCHECK(key_size_ > 0);
  FXL_DCHECK(value_size_ > 0);
  FXL_DCHECK(commit_count_ > 0);
  cloud_provider_factory_.Init();
}

void BacklogBenchmark::SyncStateChanged(SyncState download, SyncState upload,
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
  bool ret = files::CreateDirectory(writer_path);
  FXL_DCHECK(ret);

  Status status = GetLedger(
      component_context_.get(), writer_controller_.NewRequest(), nullptr, "", "backlog",
      DetachedPath(std::move(writer_path)), []() { FXL_LOG(INFO) << "Writer closed."; }, &writer_,
      kDefaultGarbageCollectionPolicy);
  if (QuitOnError(QuitLoopClosure(), status, "Get writer ledger")) {
    return;
  }

  GetPageEnsureInitialized(
      &writer_, nullptr, DelayCallback::YES, []() { FXL_LOG(INFO) << "Writer page closed."; },
      [this](Status status, PagePtr writer_page, PageId page_id) {
        if (QuitOnError(QuitLoopClosure(), status, "Writer page initialization")) {
          return;
        }

        writer_page_ = std::move(writer_page);
        page_id_ = page_id;

        TRACE_ASYNC_BEGIN("benchmark", "populate", 0);
        Populate();
      });
}

void BacklogBenchmark::Populate() {
  int transaction_size =
      static_cast<int>(ceil(static_cast<double>(unique_key_count_) / commit_count_));
  int key_count = std::max(unique_key_count_, commit_count_);
  FXL_LOG(INFO) << "Transaction size: " << transaction_size << ", key count: " << key_count << ".";
  auto keys = generator_.MakeKeys(key_count, key_size_, unique_key_count_);
  page_data_generator_.Populate(
      &writer_page_, std::move(keys), value_size_, transaction_size, reference_strategy_,
      Priority::EAGER, [this](Status status) {
        if (status != Status::OK) {
          if (QuitOnError(QuitLoopClosure(), status, "PageGenerator::Populate")) {
            return;
          }
          return;
        }
        TRACE_ASYNC_END("benchmark", "populate", 0);
        DisconnectAndRecordWriter();
      });
}

void BacklogBenchmark::DisconnectAndRecordWriter() {
  KillLedgerProcess(&writer_controller_);
  RecordDirectorySize("writer_directory_size", writer_tmp_dir_.path());
  ConnectUploader();
}

void BacklogBenchmark::ConnectUploader() {
  // Name of the storage directory currently identifies the user. Ensure the
  // most nested directory has the same name to make the ledgers sync.
  std::string uploader_path = writer_tmp_dir_.path() + kUserDirectory;

  cloud_provider::CloudProviderPtr cloud_provider_uploader;
  cloud_provider_factory_.MakeCloudProvider(user_id_, cloud_provider_uploader.NewRequest());
  Status status = GetLedger(component_context_.get(), uploader_controller_.NewRequest(),
                            std::move(cloud_provider_uploader), user_id_.user_id(), "backlog",
                            DetachedPath(std::move(uploader_path)), QuitLoopClosure(), &uploader_,
                            kDefaultGarbageCollectionPolicy);
  if (QuitOnError(QuitLoopClosure(), status, "Get uploader ledger")) {
    return;
  }

  TRACE_ASYNC_BEGIN("benchmark", "get_uploader_page", 0);
  TRACE_ASYNC_BEGIN("benchmark", "upload", 0);
  uploader_page_.set_error_handler(
      QuitOnErrorCallback(QuitLoopClosure(), "uploader page connection"));
  uploader_->GetPage(fidl::MakeOptional(page_id_), uploader_page_.NewRequest());
  uploader_->Sync([this] {
    TRACE_ASYNC_END("benchmark", "get_uploader_page", 0);
    WaitForUploaderUpload();
  });
}

void BacklogBenchmark::WaitForUploaderUpload() {
  on_sync_state_changed_ = [this](SyncState download, SyncState upload) {
    if (upload == SyncState::IDLE) {
      on_sync_state_changed_ = nullptr;
      TRACE_ASYNC_END("benchmark", "upload", 0);
      // Stop watching sync state for this page.
      sync_watcher_binding_.Unbind();
      ConnectReader();
      return;
    }
  };
  uploader_page_->SetSyncStateWatcher(sync_watcher_binding_.NewBinding());
}

void BacklogBenchmark::ConnectReader() {
  std::string reader_path = reader_tmp_dir_.path() + kUserDirectory;
  bool ret = files::CreateDirectory(reader_path);
  FXL_DCHECK(ret);

  cloud_provider::CloudProviderPtr cloud_provider_reader;
  cloud_provider_factory_.MakeCloudProvider(user_id_, cloud_provider_reader.NewRequest());
  Status status = GetLedger(component_context_.get(), reader_controller_.NewRequest(),
                            std::move(cloud_provider_reader), user_id_.user_id(), "backlog",
                            DetachedPath(std::move(reader_path)), QuitLoopClosure(), &reader_,
                            kDefaultGarbageCollectionPolicy);
  if (QuitOnError(QuitLoopClosure(), status, "ConnectReader")) {
    return;
  }

  TRACE_ASYNC_BEGIN("benchmark", "download", 0);
  TRACE_ASYNC_BEGIN("benchmark", "get_reader_page", 0);
  reader_page_.set_error_handler(QuitOnErrorCallback(QuitLoopClosure(), "reader page connection"));
  reader_->GetPage(fidl::MakeOptional(page_id_), reader_page_.NewRequest());
  reader_->Sync([this] {
    TRACE_ASYNC_END("benchmark", "get_reader_page", 0);
    WaitForReaderDownload();
  });
}

void BacklogBenchmark::WaitForReaderDownload() {
  on_sync_state_changed_ = [this](SyncState download, SyncState upload) {
    if (download == SyncState::IDLE) {
      on_sync_state_changed_ = nullptr;
      TRACE_ASYNC_END("benchmark", "download", 0);
      GetReaderSnapshot();
      return;
    }
  };
  reader_page_->SetSyncStateWatcher(sync_watcher_binding_.NewBinding());
}

void BacklogBenchmark::GetReaderSnapshot() {
  reader_page_->GetSnapshot(reader_snapshot_.NewRequest(), {}, nullptr);
  TRACE_ASYNC_BEGIN("benchmark", "get_all_entries", 0);
  GetEntriesStep(nullptr, unique_key_count_);
}

void BacklogBenchmark::CheckStatusAndGetMore(size_t entries_left,
                                             std::unique_ptr<Token> next_token) {
  if (!next_token) {
    TRACE_ASYNC_END("benchmark", "get_all_entries", 0);
    FXL_DCHECK(entries_left == 0);
    ShutDown();
    RecordDirectorySize("uploader_directory_size", writer_tmp_dir_.path());
    RecordDirectorySize("reader_directory_size", reader_tmp_dir_.path());
    return;
  }
  GetEntriesStep(std::move(next_token), entries_left);
}

void BacklogBenchmark::GetEntriesStep(std::unique_ptr<Token> token, size_t entries_left) {
  FXL_DCHECK(entries_left > 0);
  TRACE_ASYNC_BEGIN("benchmark", "get_entries_partial", entries_left);
  if (reference_strategy_ == PageDataGenerator::ReferenceStrategy::INLINE) {
    reader_snapshot_->GetEntriesInline(
        {}, std::move(token), [this, entries_left](auto entries, auto next_token) mutable {
          TRACE_ASYNC_END("benchmark", "get_entries_partial", entries_left);
          CheckStatusAndGetMore(entries_left - entries.size(), std::move(next_token));
        });
  } else {
    reader_snapshot_->GetEntriesInline(
        {}, std::move(token), [this, entries_left](auto entries, auto next_token) mutable {
          TRACE_ASYNC_END("benchmark", "get_entries_partial", entries_left);
          CheckStatusAndGetMore(entries_left - entries.size(), std::move(next_token));
        });
  }
}

void BacklogBenchmark::RecordDirectorySize(const std::string& event_name, const std::string& path) {
  uint64_t tmp_dir_size = 0;
  FXL_CHECK(GetDirectoryContentSize(DetachedPath(path), &tmp_dir_size));
  TRACE_COUNTER("benchmark", event_name.c_str(), 0, "directory_size", TA_UINT64(tmp_dir_size));
}

void BacklogBenchmark::ShutDown() {
  KillLedgerProcess(&uploader_controller_);
  KillLedgerProcess(&reader_controller_);
  loop_->Quit();
}

fit::closure BacklogBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

int Main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::Create();

  std::string unique_key_count_str;
  size_t unique_key_count;
  std::string key_size_str;
  size_t key_size;
  std::string value_size_str;
  size_t value_size;
  std::string commit_count_str;
  size_t commit_count;
  std::string reference_strategy_str;
  SyncParams sync_params;
  if (!command_line.GetOptionValue(kUniqueKeyCountFlag.ToString(), &unique_key_count_str) ||
      !fxl::StringToNumberWithError(unique_key_count_str, &unique_key_count) ||
      unique_key_count <= 0 ||
      !command_line.GetOptionValue(kKeySizeFlag.ToString(), &key_size_str) ||
      !fxl::StringToNumberWithError(key_size_str, &key_size) || key_size == 0 ||
      !command_line.GetOptionValue(kValueSizeFlag.ToString(), &value_size_str) ||
      !fxl::StringToNumberWithError(value_size_str, &value_size) || value_size <= 0 ||
      !command_line.GetOptionValue(kCommitCountFlag.ToString(), &commit_count_str) ||
      !fxl::StringToNumberWithError(commit_count_str, &commit_count) || commit_count <= 0 ||
      !command_line.GetOptionValue(kRefsFlag.ToString(), &reference_strategy_str) ||
      !ParseSyncParamsFromCommandLine(command_line, component_context.get(), &sync_params)) {
    PrintUsage();
    return -1;
  }

  PageDataGenerator::ReferenceStrategy reference_strategy;
  if (reference_strategy_str == kRefsOnFlag) {
    reference_strategy = PageDataGenerator::ReferenceStrategy::REFERENCE;
  } else if (reference_strategy_str == kRefsOffFlag) {
    reference_strategy = PageDataGenerator::ReferenceStrategy::INLINE;
  } else {
    std::cerr << "Unknown option " << reference_strategy_str << " for " << kRefsFlag.ToString()
              << std::endl;
    PrintUsage();
    return -1;
  }

  BacklogBenchmark app(&loop, std::move(component_context), unique_key_count, key_size, value_size,
                       commit_count, reference_strategy, std::move(sync_params));
  return RunWithTracing(&loop, [&app] { app.Run(); });
}

}  // namespace
}  // namespace ledger

int main(int argc, const char** argv) { return ledger::Main(argc, argv); }
