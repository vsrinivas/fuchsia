// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/tests/benchmark/convergence/convergence.h"

#include <trace/event.h>
#include <zx/time.h>

#include <iostream>

#include "garnet/lib/callback/waiter.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/fxl/command_line.h"
#include "lib/fxl/files/directory.h"
#include "lib/fxl/logging.h"
#include "lib/fxl/strings/string_number_conversions.h"
#include "peridot/bin/ledger/testing/get_ledger.h"
#include "peridot/bin/ledger/testing/quit_on_error.h"
#include "peridot/bin/ledger/testing/run_with_tracing.h"
#include "peridot/lib/convert/convert.h"

namespace {
constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/convergence";
constexpr fxl::StringView kEntryCountFlag = "entry-count";
constexpr fxl::StringView kValueSizeFlag = "value-size";
constexpr fxl::StringView kDeviceCountFlag = "device-count";
constexpr fxl::StringView kServerIdFlag = "server-id";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: " << executable_name << " --" << kEntryCountFlag
            << "=<int> --" << kValueSizeFlag << "=<int> --" << kDeviceCountFlag
            << "=<int> --" << kServerIdFlag << "=<string>" << std::endl;
}

constexpr size_t kKeySize = 100;

}  // namespace

namespace test {
namespace benchmark {

ConvergenceBenchmark::ConvergenceBenchmark(int entry_count,
                                           int value_size,
                                           int device_count,
                                           std::string server_id)
    : application_context_(
          component::ApplicationContext::CreateFromStartupInfo()),
      cloud_provider_firebase_factory_(application_context_.get()),
      entry_count_(entry_count),
      value_size_(value_size),
      device_count_(device_count),
      server_id_(std::move(server_id)),
      devices_(device_count) {
  FXL_DCHECK(entry_count_ > 0);
  FXL_DCHECK(value_size_ > 0);
  FXL_DCHECK(device_count_ > 1);
  for (auto& device_context : devices_) {
    device_context.storage_directory = files::ScopedTempDir(kStoragePath);
    device_context.page_watcher =
        std::make_unique<f1dl::Binding<ledger::PageWatcher>>(this);
  }
  page_id_ = generator_.MakePageId();
  cloud_provider_firebase_factory_.Init();
}

void ConvergenceBenchmark::Run() {
  auto waiter =
      callback::StatusWaiter<ledger::Status>::Create(ledger::Status::OK);
  for (auto& device_context : devices_) {
    // Initialize ledgers in different paths to emulate separate devices,
    // but with the same lowest-level directory name, so they correspond to the
    // same "user".
    std::string synced_dir_path =
        device_context.storage_directory.path() + "/convergence_user";
    bool ret = files::CreateDirectory(synced_dir_path);
    FXL_DCHECK(ret);

    cloud_provider::CloudProviderPtr cloud_provider;
    cloud_provider_firebase_factory_.MakeCloudProvider(
        server_id_, "", cloud_provider.NewRequest());
    ledger::Status status = test::GetLedger(
        fsl::MessageLoop::GetCurrent(), application_context_.get(),
        &device_context.app_controller, std::move(cloud_provider),
        "convergence", synced_dir_path, &device_context.ledger);
    QuitOnError(status, "GetLedger");
    device_context.ledger->GetPage(page_id_.Clone(),
                                   device_context.page_connection.NewRequest(),
                                   benchmark::QuitOnErrorCallback("GetPage"));
    ledger::PageSnapshotPtr snapshot;
    // Register a watcher; we don't really need the snapshot.
    device_context.page_connection->GetSnapshot(
        snapshot.NewRequest(), nullptr,
        device_context.page_watcher->NewBinding(), waiter->NewCallback());
  }
  waiter->Finalize([this](ledger::Status status) {
    if (benchmark::QuitOnError(status, "GetSnapshot")) {
      return;
    }
    Start(0);
  });
}

void ConvergenceBenchmark::Start(int step) {
  if (step == entry_count_) {
    ShutDown();
    return;
  }

  for (int device_id = 0; device_id < device_count_; device_id++) {
    f1dl::VectorPtr<uint8_t> key =
        generator_.MakeKey(device_count_ * step + device_id, kKeySize);
    // Insert each key N times, as we will receive N notifications - one for
    // each connection, sender included.
    for (int receiving_device = 0; receiving_device < device_count_;
         receiving_device++) {
      remaining_keys_.insert(convert::ToString(key));
    }
    f1dl::VectorPtr<uint8_t> value = generator_.MakeValue(value_size_);
    devices_[device_id].page_connection->Put(
        std::move(key), std::move(value),
        benchmark::QuitOnErrorCallback("Put"));
  }

  TRACE_ASYNC_BEGIN("benchmark", "convergence", step);
  // Persist the current step, so that we know which async event to end in
  // OnChange().
  current_step_ = step;
}

void ConvergenceBenchmark::OnChange(ledger::PageChangePtr page_change,
                                    ledger::ResultState result_state,
                                    const OnChangeCallback& callback) {
  FXL_DCHECK(result_state == ledger::ResultState::COMPLETED);
  for (auto& change : *page_change->changed_entries) {
    auto find_one = remaining_keys_.find(convert::ToString(change->key));
    remaining_keys_.erase(find_one);
  }
  if (remaining_keys_.empty()) {
    TRACE_ASYNC_END("benchmark", "convergence", current_step_);
    Start(current_step_ + 1);
  }
  callback(nullptr);
}

void ConvergenceBenchmark::ShutDown() {
  for (auto& device_context : devices_) {
    device_context.app_controller->Kill();
    device_context.app_controller.WaitForResponseUntil(
        zx::deadline_after(zx::sec(5)));
  }

  fsl::MessageLoop::GetCurrent()->PostQuitTask();
}
}  // namespace benchmark
}  // namespace test

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  std::string entry_count_str;
  int entry_count;
  std::string value_size_str;
  int value_size;
  std::string device_count_str;
  int device_count;
  std::string server_id;
  if (!command_line.GetOptionValue(kEntryCountFlag.ToString(),
                                   &entry_count_str) ||
      !fxl::StringToNumberWithError(entry_count_str, &entry_count) ||
      entry_count <= 0 ||
      !command_line.GetOptionValue(kValueSizeFlag.ToString(),
                                   &value_size_str) ||
      !fxl::StringToNumberWithError(value_size_str, &value_size) ||
      value_size <= 0 ||
      !command_line.GetOptionValue(kDeviceCountFlag.ToString(),
                                   &device_count_str) ||
      !fxl::StringToNumberWithError(device_count_str, &device_count) ||
      device_count <= 0 ||
      !command_line.GetOptionValue(kServerIdFlag.ToString(), &server_id)) {
    PrintUsage(argv[0]);
    return -1;
  }

  fsl::MessageLoop loop;
  test::benchmark::ConvergenceBenchmark app(entry_count, value_size,
                                            device_count, server_id);
  return test::benchmark::RunWithTracing(&loop, [&app] { app.Run(); });
}
