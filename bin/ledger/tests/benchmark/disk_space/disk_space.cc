// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/tests/benchmark/disk_space/disk_space.h"

#include <iostream>

#include <lib/async/cpp/task.h>
#include <lib/callback/waiter.h>
#include <lib/fit/function.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <lib/fxl/strings/string_number_conversions.h>
#include <lib/zx/time.h>
#include <trace/event.h>

#include "garnet/public/lib/callback/waiter.h"
#include "peridot/bin/ledger/filesystem/get_directory_content_size.h"
#include "peridot/bin/ledger/testing/get_ledger.h"
#include "peridot/bin/ledger/testing/quit_on_error.h"
#include "peridot/bin/ledger/testing/run_with_tracing.h"

namespace {
constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/disk_space";
constexpr fxl::StringView kPageCountFlag = "page-count";
constexpr fxl::StringView kUniqueKeyCountFlag = "unique-key-count";
constexpr fxl::StringView kCommitCountFlag = "commit-count";
constexpr fxl::StringView kKeySizeFlag = "key-size";
constexpr fxl::StringView kValueSizeFlag = "value-size";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: trace record "
            << executable_name
            // Comment to make clang format not break formatting.
            << " --" << kPageCountFlag << "=<int>"
            << " --" << kUniqueKeyCountFlag << "=<int>"
            << " --" << kCommitCountFlag << "=<int>"
            << " --" << kKeySizeFlag << "=<int>"
            << " --" << kValueSizeFlag << "=<int>" << std::endl;
}

}  // namespace

namespace ledger {

DiskSpaceBenchmark::DiskSpaceBenchmark(async::Loop* loop, size_t page_count,
                                       size_t unique_key_count,
                                       size_t commit_count, size_t key_size,
                                       size_t value_size)
    : loop_(loop),
      tmp_dir_(kStoragePath),
      startup_context_(component::StartupContext::CreateFromStartupInfo()),
      page_count_(page_count),
      unique_key_count_(unique_key_count),
      commit_count_(commit_count),
      key_size_(key_size),
      value_size_(value_size) {
  FXL_DCHECK(loop_);
  FXL_DCHECK(page_count_ >= 0);
  FXL_DCHECK(unique_key_count_ >= 0);
  FXL_DCHECK(commit_count_ >= 0);
  FXL_DCHECK(key_size_ > 0);
  FXL_DCHECK(value_size_ > 0);
}

void DiskSpaceBenchmark::Run() {
  GetLedger(
      startup_context_.get(), component_controller_.NewRequest(), nullptr,
      "disk_space", DetachedPath(tmp_dir_.path()), QuitLoopClosure(),
      [this](Status status, LedgerPtr ledger) {
        if (QuitOnError(QuitLoopClosure(), status, "GetLedger")) {
          return;
        }
        ledger_ = std::move(ledger);

        auto waiter =
            fxl::MakeRefCounted<callback::Waiter<Status, PagePtr>>(Status::OK);

        for (size_t page_number = 0; page_number < page_count_; page_number++) {
          GetPageEnsureInitialized(&ledger_, nullptr, QuitLoopClosure(),
                                   [callback = waiter->NewCallback()](
                                       Status status, PagePtr page, PageId id) {
                                     callback(status, std::move(page));
                                   });
        }

        waiter->Finalize([this](Status status, std::vector<PagePtr> pages) {
          if (QuitOnError(QuitLoopClosure(), status,
                          "GetPageEnsureInitialized")) {
            return;
          }
          pages_ = std::move(pages);
          if (commit_count_ == 0) {
            ShutDownAndRecord();
            return;
          }
          Populate();
        });
      });
}

void DiskSpaceBenchmark::Populate() {
  int transaction_size = static_cast<int>(
      ceil(static_cast<double>(unique_key_count_) / commit_count_));
  int insertions = std::max(unique_key_count_, commit_count_);
  FXL_LOG(INFO) << "Transaction size: " << transaction_size
                << ", insertions: " << insertions << ".";
  auto waiter = fxl::MakeRefCounted<callback::StatusWaiter<Status>>(Status::OK);
  for (auto& page : pages_) {
    auto keys = generator_.MakeKeys(insertions, key_size_, unique_key_count_);
    page_data_generator_.Populate(
        &page, std::move(keys), value_size_, transaction_size,
        PageDataGenerator::ReferenceStrategy::REFERENCE, Priority::EAGER,
        waiter->NewCallback());
  }
  waiter->Finalize([this](Status status) {
    if (QuitOnError(QuitLoopClosure(), status, "PageGenerator::Populate")) {
      return;
    }
    ShutDownAndRecord();
  });
}

void DiskSpaceBenchmark::ShutDownAndRecord() {
  KillLedgerProcess(&component_controller_);
  loop_->Quit();

  uint64_t tmp_dir_size = 0;
  FXL_CHECK(
      GetDirectoryContentSize(DetachedPath(tmp_dir_.path()), &tmp_dir_size));
  TRACE_COUNTER("benchmark", "ledger_directory_size", 0, "directory_size",
                TA_UINT64(tmp_dir_size));
}

fit::closure DiskSpaceBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

}  // namespace ledger

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  std::string page_count_str;
  size_t page_count;
  std::string unique_key_count_str;
  size_t unique_key_count;
  std::string commit_count_str;
  size_t commit_count;
  std::string key_size_str;
  size_t key_size;
  std::string value_size_str;
  size_t value_size;
  if (!command_line.GetOptionValue(kPageCountFlag.ToString(),
                                   &page_count_str) ||
      !fxl::StringToNumberWithError(page_count_str, &page_count) ||
      !command_line.GetOptionValue(kUniqueKeyCountFlag.ToString(),
                                   &unique_key_count_str) ||
      !fxl::StringToNumberWithError(unique_key_count_str, &unique_key_count) ||
      !command_line.GetOptionValue(kCommitCountFlag.ToString(),
                                   &commit_count_str) ||
      !fxl::StringToNumberWithError(commit_count_str, &commit_count) ||
      !command_line.GetOptionValue(kKeySizeFlag.ToString(), &key_size_str) ||
      !fxl::StringToNumberWithError(key_size_str, &key_size) || key_size == 0 ||
      !command_line.GetOptionValue(kValueSizeFlag.ToString(),
                                   &value_size_str) ||
      !fxl::StringToNumberWithError(value_size_str, &value_size) ||
      value_size == 0) {
    PrintUsage(argv[0]);
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  ledger::DiskSpaceBenchmark app(&loop, page_count, unique_key_count,
                                 commit_count, key_size, value_size);

  return ledger::RunWithTracing(&loop, [&app] { app.Run(); });
}
