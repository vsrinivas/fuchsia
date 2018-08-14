// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/tests/benchmark/fetch/fetch.h"

#include <iostream>

#include <fuchsia/ledger/cloud/cpp/fidl.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/fsl/vmo/strings.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/random/uuid.h>
#include <lib/fxl/strings/string_number_conversions.h>
#include <lib/zx/time.h>
#include <trace/event.h>

#include "peridot/bin/ledger/testing/get_ledger.h"
#include "peridot/bin/ledger/testing/quit_on_error.h"
#include "peridot/bin/ledger/testing/run_with_tracing.h"
#include "peridot/lib/convert/convert.h"

namespace {
constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/fetch";
constexpr fxl::StringView kEntryCountFlag = "entry-count";
constexpr fxl::StringView kValueSizeFlag = "value-size";
constexpr fxl::StringView kPartSizeFlag = "part-size";

constexpr size_t kKeySize = 100;

const std::string kUserDirectory = "/fetch-user";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: trace record "
            << executable_name
            // Comment to make clang format not break formatting.
            << " --" << kEntryCountFlag << "=<int>"
            << " --" << kValueSizeFlag << "=<int>"
            << " --" << kPartSizeFlag << "=<int>"
            << ledger::GetSyncParamsUsage() << std::endl;
}

}  // namespace

namespace ledger {

FetchBenchmark::FetchBenchmark(async::Loop* loop, size_t entry_count,
                               size_t value_size, size_t part_size,
                               SyncParams sync_params)
    : loop_(loop),
      startup_context_(component::StartupContext::CreateFromStartupInfo()),
      cloud_provider_factory_(
          startup_context_.get(), std::move(sync_params.server_id),
          std::move(sync_params.api_key), std::move(sync_params.credentials)),
      sync_watcher_binding_(this),
      entry_count_(entry_count),
      value_size_(value_size),
      part_size_(part_size),
      user_id_("fetch_" + fxl::GenerateUUID()),
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
  cloud_provider_factory_.MakeCloudProviderWithGivenUserId(
      user_id_, cloud_provider_writer.NewRequest());
  GetLedger(startup_context_.get(), writer_controller_.NewRequest(),
            std::move(cloud_provider_writer), "fetch",
            DetachedPath(std::move(writer_path)), QuitLoopClosure(),
            [this](Status status, LedgerPtr ledger) {
              if (QuitOnError(QuitLoopClosure(), status, "Get writer ledger")) {
                return;
              }
              writer_ = std::move(ledger);

              GetPageEnsureInitialized(
                  &writer_, nullptr, QuitLoopClosure(),
                  [this](Status status, PagePtr page, PageId id) {
                    if (QuitOnError(QuitLoopClosure(), status,
                                    "Writer page initialization")) {
                      return;
                    }
                    writer_page_ = std::move(page);
                    page_id_ = id;

                    Populate();
                  });
            });
}

void FetchBenchmark::Populate() {
  auto keys = generator_.MakeKeys(entry_count_, kKeySize, entry_count_);
  for (size_t i = 0; i < entry_count_; i++) {
    keys_.push_back(keys[i].Clone());
  }

  page_data_generator_.Populate(
      &writer_page_, std::move(keys), value_size_, entry_count_,
      PageDataGenerator::ReferenceStrategy::REFERENCE, Priority::LAZY,
      [this](Status status) {
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
  writer_page_->SetSyncStateWatcher(
      sync_watcher_binding_.NewBinding(),
      QuitOnErrorCallback(QuitLoopClosure(), "Page::SetSyncStateWatcher"));
}

void FetchBenchmark::ConnectReader() {
  std::string reader_path = reader_tmp_dir_.path() + kUserDirectory;
  bool ret = files::CreateDirectory(reader_path);
  FXL_DCHECK(ret);

  cloud_provider::CloudProviderPtr cloud_provider_reader;
  cloud_provider_factory_.MakeCloudProviderWithGivenUserId(
      user_id_, cloud_provider_reader.NewRequest());
  GetLedger(startup_context_.get(), reader_controller_.NewRequest(),
            std::move(cloud_provider_reader), "fetch",
            DetachedPath(std::move(reader_path)), QuitLoopClosure(),
            [this](Status status, LedgerPtr ledger) {
              if (QuitOnError(QuitLoopClosure(), status, "ConnectReader")) {
                return;
              }

              reader_ = std::move(ledger);

              reader_->GetPage(
                  fidl::MakeOptional(page_id_), reader_page_.NewRequest(),
                  [this](Status status) {
                    if (QuitOnError(QuitLoopClosure(), status, "GetPage")) {
                      return;
                    }
                    WaitForReaderDownload();
                  });
            });
}

void FetchBenchmark::WaitForReaderDownload() {
  on_sync_state_changed_ = [this](SyncState download, SyncState upload) {
    if (download == SyncState::IDLE) {
      on_sync_state_changed_ = nullptr;
      PageSnapshotPtr snapshot;
      reader_page_->GetSnapshot(
          snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0), nullptr,
          QuitOnErrorCallback(QuitLoopClosure(), "GetSnapshot"));
      FetchValues(std::move(snapshot), 0);
      return;
    }
  };
  reader_page_->SetSyncStateWatcher(
      sync_watcher_binding_.NewBinding(),
      QuitOnErrorCallback(QuitLoopClosure(), "Page::SetSyncStateWatcher"));
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
  snapshot_ptr->Fetch(
      std::move(keys_[i]),
      [this, snapshot = std::move(snapshot), i](
          Status status, fuchsia::mem::BufferPtr value) mutable {
        if (QuitOnError(QuitLoopClosure(), status, "PageSnapshot::Fetch")) {
          return;
        }
        TRACE_ASYNC_END("benchmark", "Fetch", i);
        FetchValues(std::move(snapshot), i + 1);
      });
}

void FetchBenchmark::FetchPart(PageSnapshotPtr snapshot, size_t i,
                               size_t part) {
  if (part * part_size_ >= value_size_) {
    TRACE_ASYNC_END("benchmark", "Fetch (cumulative)", i);
    FetchValues(std::move(snapshot), i + 1);
    return;
  }
  PageSnapshot* snapshot_ptr = snapshot.get();
  auto trace_event_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("benchmark", "FetchPartial", trace_event_id);
  snapshot_ptr->FetchPartial(
      keys_[i].Clone(), part * part_size_, part_size_,
      [this, snapshot = std::move(snapshot), i, part, trace_event_id](
          Status status, fuchsia::mem::BufferPtr value) mutable {
        if (QuitOnError(QuitLoopClosure(), status,
                        "PageSnapshot::FetchPartial")) {
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

}  // namespace ledger

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  std::string entry_count_str;
  size_t entry_count;
  std::string value_size_str;
  size_t value_size;
  std::string part_size_str;
  size_t part_size;
  ledger::SyncParams sync_params;
  if (!command_line.GetOptionValue(kEntryCountFlag.ToString(),
                                   &entry_count_str) ||
      !fxl::StringToNumberWithError(entry_count_str, &entry_count) ||
      entry_count == 0 ||
      !command_line.GetOptionValue(kValueSizeFlag.ToString(),
                                   &value_size_str) ||
      !fxl::StringToNumberWithError(value_size_str, &value_size) ||
      value_size == 0 ||
      !command_line.GetOptionValue(kPartSizeFlag.ToString(), &part_size_str) ||
      !fxl::StringToNumberWithError(part_size_str, &part_size) ||
      !ledger::ParseSyncParamsFromCommandLine(command_line, &sync_params)) {
    PrintUsage(argv[0]);
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  ledger::FetchBenchmark app(&loop, entry_count, value_size, part_size,
                             std::move(sync_params));
  return ledger::RunWithTracing(&loop, [&app] { app.Run(); });
}
