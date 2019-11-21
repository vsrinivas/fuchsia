// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/time.h>

#include <iostream>
#include <memory>
#include <vector>

#include <trace/event.h>

#include "peridot/lib/rng/test_random.h"
#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/testing/data_generator.h"
#include "src/ledger/bin/testing/get_ledger.h"
#include "src/ledger/bin/testing/get_page_ensure_initialized.h"
#include "src/ledger/bin/testing/page_data_generator.h"
#include "src/ledger/bin/testing/quit_on_error.h"
#include "src/ledger/bin/testing/run_with_tracing.h"
#include "src/ledger/bin/testing/sync_params.h"
#include "src/ledger/cloud_provider_firestore/bin/testing/cloud_provider_factory.h"
#include "src/ledger/lib/convert/convert.h"
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
    "fuchsia-pkg://fuchsia.com/ledger_benchmarks#meta/fetch.cmx";
constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/fetch";
constexpr fxl::StringView kEntryCountFlag = "entry-count";
constexpr fxl::StringView kValueSizeFlag = "value-size";
constexpr fxl::StringView kPartSizeFlag = "part-size";

constexpr size_t kKeySize = 100;

const std::string kUserDirectory = "/fetch-user";

void PrintUsage() {
  std::cout << "Usage: trace record "
            << kBinaryPath
            // Comment to make clang format not break formatting.
            << " --" << kEntryCountFlag << "=<int>"
            << " --" << kValueSizeFlag << "=<int>"
            << " --" << kPartSizeFlag << "=<int>" << GetSyncParamsUsage() << std::endl;
}

// Benchmark that measures time to fetch lazy values from server.
// Parameters:
//   --entry-count=<int> the number of entries to be put
//   --value-size=<int> the size of a single value in bytes
//   --part-size=<int> the size of the part to be read with one Fetch
//   call. If equal to zero, the whole value will be read.
//   --credentials-path=<file path> Firestore service account credentials
class FetchBenchmark : public SyncWatcher {
 public:
  FetchBenchmark(async::Loop* loop, std::unique_ptr<sys::ComponentContext> component_context,
                 size_t entry_count, size_t value_size, size_t part_size, SyncParams sync_params);

  void Run();

  // SyncWatcher:
  void SyncStateChanged(SyncState download, SyncState upload,
                        SyncStateChangedCallback callback) override;

 private:
  void Populate();
  void WaitForWriterUpload();
  void ConnectReader();
  void WaitForReaderDownload();

  void FetchValues(PageSnapshotPtr snapshot, size_t i);
  void FetchPart(PageSnapshotPtr snapshot, size_t i, size_t part);

  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  rng::TestRandom random_;
  DataGenerator generator_;
  PageDataGenerator page_data_generator_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  cloud_provider_firestore::CloudProviderFactory cloud_provider_factory_;
  fidl::Binding<SyncWatcher> sync_watcher_binding_;
  const size_t entry_count_;
  const size_t value_size_;
  const size_t part_size_;
  const cloud_provider_firestore::CloudProviderFactory::UserId user_id_;
  files::ScopedTempDir writer_tmp_dir_;
  files::ScopedTempDir reader_tmp_dir_;
  fuchsia::sys::ComponentControllerPtr writer_controller_;
  fuchsia::sys::ComponentControllerPtr reader_controller_;
  LedgerPtr writer_;
  LedgerPtr reader_;
  PageId page_id_;
  PagePtr writer_page_;
  PagePtr reader_page_;
  std::vector<std::vector<uint8_t>> keys_;
  fit::function<void(SyncState, SyncState)> on_sync_state_changed_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FetchBenchmark);
};

FetchBenchmark::FetchBenchmark(async::Loop* loop,
                               std::unique_ptr<sys::ComponentContext> component_context,
                               size_t entry_count, size_t value_size, size_t part_size,
                               SyncParams sync_params)
    : loop_(loop),
      random_(0),
      generator_(&random_),
      page_data_generator_(&random_),
      component_context_(std::move(component_context)),
      cloud_provider_factory_(component_context_.get(), &random_, std::move(sync_params.api_key),
                              std::move(sync_params.credentials)),
      sync_watcher_binding_(this),
      entry_count_(entry_count),
      value_size_(value_size),
      part_size_(part_size),
      user_id_(cloud_provider_firestore::CloudProviderFactory::UserId::New()),
      writer_tmp_dir_(kStoragePath),
      reader_tmp_dir_(kStoragePath) {
  FXL_DCHECK(loop_);
  FXL_DCHECK(entry_count_ > 0);
  FXL_DCHECK(value_size_ > 0);
  FXL_DCHECK(part_size_ <= value_size);
  cloud_provider_factory_.Init();
}

void FetchBenchmark::SyncStateChanged(SyncState download, SyncState upload,
                                      SyncStateChangedCallback callback) {
  if (on_sync_state_changed_) {
    on_sync_state_changed_(download, upload);
  }
  callback();
}

void FetchBenchmark::Run() {
  // Name of the storage directory currently identifies the user. Ensure the
  // most nested directory has the same name to make the ledgers sync.
  std::string writer_path = writer_tmp_dir_.path() + kUserDirectory;
  bool ret = files::CreateDirectory(writer_path);
  FXL_DCHECK(ret);

  cloud_provider::CloudProviderPtr cloud_provider_writer;
  cloud_provider_factory_.MakeCloudProvider(user_id_, cloud_provider_writer.NewRequest());
  Status status = GetLedger(component_context_.get(), writer_controller_.NewRequest(),
                            std::move(cloud_provider_writer), user_id_.user_id(), "fetch",
                            DetachedPath(std::move(writer_path)), QuitLoopClosure(), &writer_,
                            kDefaultGarbageCollectionPolicy);
  if (QuitOnError(QuitLoopClosure(), status, "Get writer ledger")) {
    return;
  }

  GetPageEnsureInitialized(
      &writer_, nullptr, DelayCallback::YES, QuitLoopClosure(),
      [this](Status status, PagePtr page, PageId id) {
        if (QuitOnError(QuitLoopClosure(), status, "Writer page initialization")) {
          return;
        }
        writer_page_ = std::move(page);
        page_id_ = id;

        Populate();
      });
}

void FetchBenchmark::Populate() {
  auto keys = generator_.MakeKeys(entry_count_, kKeySize, entry_count_);
  for (size_t i = 0; i < entry_count_; i++) {
    keys_.push_back(keys[i]);
  }

  page_data_generator_.Populate(
      &writer_page_, std::move(keys), value_size_, entry_count_,
      PageDataGenerator::ReferenceStrategy::REFERENCE, Priority::LAZY, [this](Status status) {
        if (QuitOnError(QuitLoopClosure(), status, "PageGenerator::Populate")) {
          return;
        }
        WaitForWriterUpload();
      });
}

void FetchBenchmark::WaitForWriterUpload() {
  on_sync_state_changed_ = [this](SyncState download, SyncState upload) {
    if (upload == SyncState::IDLE) {
      on_sync_state_changed_ = nullptr;
      // Stop watching sync state for this page.
      sync_watcher_binding_.Unbind();
      ConnectReader();
      return;
    }
  };
  writer_page_->SetSyncStateWatcher(sync_watcher_binding_.NewBinding());
}

void FetchBenchmark::ConnectReader() {
  std::string reader_path = reader_tmp_dir_.path() + kUserDirectory;
  bool ret = files::CreateDirectory(reader_path);
  FXL_DCHECK(ret);

  cloud_provider::CloudProviderPtr cloud_provider_reader;
  cloud_provider_factory_.MakeCloudProvider(user_id_, cloud_provider_reader.NewRequest());
  Status status = GetLedger(component_context_.get(), reader_controller_.NewRequest(),
                            std::move(cloud_provider_reader), user_id_.user_id(), "fetch",
                            DetachedPath(std::move(reader_path)), QuitLoopClosure(), &reader_,
                            kDefaultGarbageCollectionPolicy);
  if (QuitOnError(QuitLoopClosure(), status, "ConnectReader")) {
    return;
  }

  reader_->GetPage(fidl::MakeOptional(page_id_), reader_page_.NewRequest());
  WaitForReaderDownload();
}

void FetchBenchmark::WaitForReaderDownload() {
  on_sync_state_changed_ = [this](SyncState download, SyncState upload) {
    if (download == SyncState::IDLE) {
      on_sync_state_changed_ = nullptr;
      PageSnapshotPtr snapshot;
      reader_page_->GetSnapshot(snapshot.NewRequest(), {}, nullptr);
      FetchValues(std::move(snapshot), 0);
      return;
    }
  };
  reader_page_->SetSyncStateWatcher(sync_watcher_binding_.NewBinding());
}

void FetchBenchmark::FetchValues(PageSnapshotPtr snapshot, size_t i) {
  if (i >= entry_count_) {
    ShutDown();
    return;
  }

  if (part_size_ > 0) {
    TRACE_ASYNC_BEGIN("benchmark", "Fetch (cumulative)", i);
    FetchPart(std::move(snapshot), i, 0);
    return;
  }
  PageSnapshot* snapshot_ptr = snapshot.get();

  TRACE_ASYNC_BEGIN("benchmark", "Fetch", i);
  snapshot_ptr->Fetch(std::move(keys_[i]),
                      [this, snapshot = std::move(snapshot),
                       i](fuchsia::ledger::PageSnapshot_Fetch_Result result) mutable {
                        if (QuitOnError(QuitLoopClosure(), result, "PageSnapshot::Fetch")) {
                          return;
                        }
                        TRACE_ASYNC_END("benchmark", "Fetch", i);
                        FetchValues(std::move(snapshot), i + 1);
                      });
}

void FetchBenchmark::FetchPart(PageSnapshotPtr snapshot, size_t i, size_t part) {
  if (part * part_size_ >= value_size_) {
    TRACE_ASYNC_END("benchmark", "Fetch (cumulative)", i);
    FetchValues(std::move(snapshot), i + 1);
    return;
  }
  PageSnapshot* snapshot_ptr = snapshot.get();
  auto trace_event_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("benchmark", "FetchPartial", trace_event_id);
  snapshot_ptr->FetchPartial(
      keys_[i], part * part_size_, part_size_,
      [this, snapshot = std::move(snapshot), i, part,
       trace_event_id](fuchsia::ledger::PageSnapshot_FetchPartial_Result result) mutable {
        if (QuitOnError(QuitLoopClosure(), result, "PageSnapshot::FetchPartial")) {
          return;
        }
        TRACE_ASYNC_END("benchmark", "FetchPartial", trace_event_id);
        FetchPart(std::move(snapshot), i, part + 1);
      });
}

void FetchBenchmark::ShutDown() {
  KillLedgerProcess(&writer_controller_);
  KillLedgerProcess(&reader_controller_);
  loop_->Quit();
}

fit::closure FetchBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

int Main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::Create();

  std::string entry_count_str;
  size_t entry_count;
  std::string value_size_str;
  size_t value_size;
  std::string part_size_str;
  size_t part_size;
  SyncParams sync_params;
  if (!command_line.GetOptionValue(kEntryCountFlag.ToString(), &entry_count_str) ||
      !fxl::StringToNumberWithError(entry_count_str, &entry_count) || entry_count == 0 ||
      !command_line.GetOptionValue(kValueSizeFlag.ToString(), &value_size_str) ||
      !fxl::StringToNumberWithError(value_size_str, &value_size) || value_size == 0 ||
      !command_line.GetOptionValue(kPartSizeFlag.ToString(), &part_size_str) ||
      !fxl::StringToNumberWithError(part_size_str, &part_size) ||
      !ParseSyncParamsFromCommandLine(command_line, component_context.get(), &sync_params)) {
    PrintUsage();
    return -1;
  }

  FetchBenchmark app(&loop, std::move(component_context), entry_count, value_size, part_size,
                     std::move(sync_params));
  return RunWithTracing(&loop, [&app] { app.Run(); });
}

}  // namespace
}  // namespace ledger

int main(int argc, const char** argv) { return ledger::Main(argc, argv); }
