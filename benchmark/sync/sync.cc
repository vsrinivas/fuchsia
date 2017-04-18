// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/benchmark/sync/sync.h"

#include <iostream>

#include "apps/ledger/benchmark/lib/convert.h"
#include "apps/ledger/benchmark/lib/get_ledger.h"
#include "apps/ledger/benchmark/lib/logging.h"
#include "apps/tracing/lib/trace/event.h"
#include "apps/tracing/lib/trace/provider.h"
#include "lib/ftl/command_line.h"
#include "lib/ftl/files/directory.h"
#include "lib/ftl/logging.h"
#include "lib/ftl/strings/string_number_conversions.h"
#include "lib/mtl/tasks/message_loop.h"

namespace {
constexpr ftl::StringView kStoragePath = "/data/benchmark/ledger/sync";
constexpr ftl::StringView kEntryCountFlag = "entry-count";
constexpr ftl::StringView kValueSizeFlag = "value-size";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kEntryCountFlag
            << "=<int> --" << kValueSizeFlag << "=<int>" << std::endl;
}

fidl::Array<uint8_t> MakeKey(int i) {
  return benchmark::ToArray(std::to_string(i));
}

}  // namespace

namespace benchmark {

SyncBenchmark::SyncBenchmark(int entry_count, int value_size)
    : application_context_(app::ApplicationContext::CreateFromStartupInfo()),
      entry_count_(entry_count),
      value_(fidl::Array<uint8_t>::New(value_size)),
      page_watcher_binding_(this),
      alpha_tmp_dir_(kStoragePath),
      beta_tmp_dir_(kStoragePath) {
  FTL_DCHECK(entry_count > 0);
  FTL_DCHECK(value_size > 0);
  tracing::InitializeTracer(application_context_.get(),
                            {"benchmark_ledger_sync"});
}

void SyncBenchmark::Run() {
  // Name of the storage directory currently identifies the user. Ensure the
  // most nested directory has the same name to make the ledgers sync.
  std::string alpha_path = alpha_tmp_dir_.path() + "/sync_user";
  bool ret = files::CreateDirectory(alpha_path);
  FTL_DCHECK(ret);

  std::string beta_path = beta_tmp_dir_.path() + "/sync_user";
  ret = files::CreateDirectory(beta_path);
  FTL_DCHECK(ret);

  ledger::LedgerPtr alpha = benchmark::GetLedger(
      application_context_.get(), &alpha_controller_, "sync", alpha_path);
  ledger::LedgerPtr beta = benchmark::GetLedger(
      application_context_.get(), &beta_controller_, "sync", beta_path);

  benchmark::GetRootPageEnsureInitialized(alpha.get(),
                                          [this](ledger::PagePtr page) {
                                            alpha_page_ = std::move(page);
                                            alpha_ready_ = true;
                                            if (beta_ready_) {
                                              RunSingle(0);
                                            }
                                          });

  beta->GetRootPage(beta_page_.NewRequest(),
                    benchmark::QuitOnErrorCallback("GetRootPage"));
  ledger::PageSnapshotPtr snapshot;
  beta_page_->GetSnapshot(snapshot.NewRequest(), nullptr,
                          page_watcher_binding_.NewBinding(),
                          [this](ledger::Status status) {
                            if (benchmark::QuitOnError(status, "GetSnapshot")) {
                              return;
                            }
                            beta_ready_ = true;
                            if (alpha_ready_) {
                              RunSingle(0);
                            }
                          });
}

void SyncBenchmark::OnChange(ledger::PageChangePtr page_change,
                             ledger::ResultState result_state,
                             const OnChangeCallback& callback) {
  FTL_DCHECK(page_change->changes.size() == 1);
  FTL_DCHECK(result_state == ledger::ResultState::COMPLETED);
  int i = std::stoi(benchmark::ToString(page_change->changes[0]->key));
  TRACE_ASYNC_END("benchmark", "sync latency", i);
  RunSingle(i + 1);
  callback(nullptr);
}

void SyncBenchmark::RunSingle(int i) {
  FTL_DCHECK(alpha_ready_ && beta_ready_);
  if (i == entry_count_) {
    ShutDown();
    return;
  }

  fidl::Array<uint8_t> key = MakeKey(i);
  fidl::Array<uint8_t> value = value_.Clone();
  TRACE_ASYNC_BEGIN("benchmark", "sync latency", i);
  alpha_page_->Put(std::move(key), std::move(value),
                   benchmark::QuitOnErrorCallback("Put"));
}

void SyncBenchmark::ShutDown() {
  alpha_controller_->Kill();
  alpha_controller_.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(5));
  beta_controller_->Kill();
  beta_controller_.WaitForIncomingResponseWithTimeout(
      ftl::TimeDelta::FromSeconds(5));
  mtl::MessageLoop::GetCurrent()->PostQuitTask();
}
}  // namespace benchmark

int main(int argc, const char** argv) {
  ftl::CommandLine command_line = ftl::CommandLineFromArgcArgv(argc, argv);

  std::string entry_count_str;
  int entry_count;
  std::string value_size_str;
  int value_size;
  if (!command_line.GetOptionValue(kEntryCountFlag.ToString(),
                                   &entry_count_str) ||
      !ftl::StringToNumberWithError(entry_count_str, &entry_count) ||
      entry_count <= 0 ||
      !command_line.GetOptionValue(kValueSizeFlag.ToString(),
                                   &value_size_str) ||
      !ftl::StringToNumberWithError(value_size_str, &value_size) ||
      value_size <= 0) {
    PrintUsage(argv[0]);
    return -1;
  }

  mtl::MessageLoop loop;
  benchmark::SyncBenchmark app(entry_count, value_size);
  loop.task_runner()->PostTask([&app] { app.Run(); });
  loop.Run();
  return 0;
}
