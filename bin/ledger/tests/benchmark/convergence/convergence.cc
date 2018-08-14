// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/tests/benchmark/convergence/convergence.h"

#include <iostream>

#include <lib/callback/waiter.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/fxl/command_line.h>
#include <lib/fxl/files/directory.h>
#include <lib/fxl/files/file.h>
#include <lib/fxl/logging.h>
#include <lib/fxl/memory/ref_ptr.h>
#include <lib/fxl/random/uuid.h>
#include <lib/fxl/strings/string_number_conversions.h>
#include <lib/zx/time.h>
#include <trace/event.h>

#include "peridot/bin/ledger/testing/get_ledger.h"
#include "peridot/bin/ledger/testing/quit_on_error.h"
#include "peridot/bin/ledger/testing/run_with_tracing.h"
#include "peridot/lib/convert/convert.h"

namespace {
constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/convergence";
constexpr fxl::StringView kEntryCountFlag = "entry-count";
constexpr fxl::StringView kValueSizeFlag = "value-size";
constexpr fxl::StringView kDeviceCountFlag = "device-count";

void PrintUsage(const char* executable_name) {
  std::cout << "Usage: trace record "
            << executable_name
            // Comment to make clang format not break formatting.
            << " --" << kEntryCountFlag << "=<int>"
            << " --" << kValueSizeFlag << "=<int>"
            << " --" << kDeviceCountFlag << "=<int>"
            << ledger::GetSyncParamsUsage() << std::endl;
}

constexpr size_t kKeySize = 100;

}  // namespace

namespace ledger {

// Instances needed to control the Ledger process associated with a device and
// interact with it.
struct ConvergenceBenchmark::DeviceContext {
  std::unique_ptr<files::ScopedTempDir> storage_directory;
  fuchsia::sys::ComponentControllerPtr controller;
  LedgerPtr ledger;
  PagePtr page_connection;
  std::unique_ptr<fidl::Binding<PageWatcher>> page_watcher;
};

ConvergenceBenchmark::ConvergenceBenchmark(async::Loop* loop, int entry_count,
                                           int value_size, int device_count,
                                           SyncParams sync_params)
    : loop_(loop),
      startup_context_(component::StartupContext::CreateFromStartupInfo()),
      cloud_provider_factory_(
          startup_context_.get(), std::move(sync_params.server_id),
          std::move(sync_params.api_key), std::move(sync_params.credentials)),
      entry_count_(entry_count),
      value_size_(value_size),
      device_count_(device_count),
      user_id_("convergence_" + fxl::GenerateUUID()),
      devices_(device_count) {
  FXL_DCHECK(loop_);
  FXL_DCHECK(entry_count_ > 0);
  FXL_DCHECK(value_size_ > 0);
  FXL_DCHECK(device_count_ > 1);
  for (auto& device_context : devices_) {
    device_context = std::make_unique<DeviceContext>();
    device_context->storage_directory =
        std::make_unique<files::ScopedTempDir>(kStoragePath);
    device_context->page_watcher =
        std::make_unique<fidl::Binding<PageWatcher>>(this);
  }
  page_id_ = generator_.MakePageId();
  cloud_provider_factory_.Init();
}

void ConvergenceBenchmark::Run() {
  auto waiter = fxl::MakeRefCounted<callback::StatusWaiter<Status>>(Status::OK);
  for (auto& device_context : devices_) {
    // Initialize ledgers in different paths to emulate separate devices,
    // but with the same lowest-level directory name, so they correspond to the
    // same "user".
    std::string synced_dir_path =
        device_context->storage_directory->path() + "/convergence_user";
    bool ret = files::CreateDirectory(synced_dir_path);
    FXL_DCHECK(ret);

    cloud_provider::CloudProviderPtr cloud_provider;
    cloud_provider_factory_.MakeCloudProviderWithGivenUserId(
        user_id_, cloud_provider.NewRequest());

    GetLedger(startup_context_.get(), device_context->controller.NewRequest(),
              std::move(cloud_provider), "convergence",
              DetachedPath(synced_dir_path), QuitLoopClosure(),
              [this, device_context = device_context.get(),
               callback = waiter->NewCallback()](Status status,
                                                 LedgerPtr ledger) mutable {
                if (QuitOnError(QuitLoopClosure(), status, "GetLedger")) {
                  return;
                }
                device_context->ledger = std::move(ledger);
                device_context->ledger->GetPage(
                    fidl::MakeOptional(page_id_),
                    device_context->page_connection.NewRequest(),
                    QuitOnErrorCallback(QuitLoopClosure(), "GetPage"));
                PageSnapshotPtr snapshot;
                // Register a watcher; we don't really need the snapshot.
                device_context->page_connection->GetSnapshot(
                    snapshot.NewRequest(), fidl::VectorPtr<uint8_t>::New(0),
                    device_context->page_watcher->NewBinding(),
                    std::move(callback));
              });
  }
  waiter->Finalize([this](Status status) {
    if (QuitOnError(QuitLoopClosure(), status, "GetSnapshot")) {
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
    fidl::VectorPtr<uint8_t> key =
        generator_.MakeKey(device_count_ * step + device_id, kKeySize);
    // Insert each key N times, as we will receive N notifications - one for
    // each connection, sender included.
    for (int receiving_device = 0; receiving_device < device_count_;
         receiving_device++) {
      remaining_keys_.insert(convert::ToString(key));
    }
    fidl::VectorPtr<uint8_t> value = generator_.MakeValue(value_size_);
    devices_[device_id]->page_connection->Put(
        std::move(key), std::move(value),
        QuitOnErrorCallback(QuitLoopClosure(), "Put"));
  }

  TRACE_ASYNC_BEGIN("benchmark", "convergence", step);
  // Persist the current step, so that we know which dispatcher event to end in
  // OnChange().
  current_step_ = step;
}

void ConvergenceBenchmark::OnChange(PageChange page_change,
                                    ResultState result_state,
                                    OnChangeCallback callback) {
  FXL_DCHECK(result_state == ResultState::COMPLETED);
  for (auto& change : *page_change.changed_entries) {
    auto find_one = remaining_keys_.find(convert::ToString(change.key));
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
    KillLedgerProcess(&device_context->controller);
  }
  loop_->Quit();
}

fit::closure ConvergenceBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

}  // namespace ledger

int main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);

  std::string entry_count_str;
  int entry_count;
  std::string value_size_str;
  int value_size;
  std::string device_count_str;
  int device_count;
  ledger::SyncParams sync_params;
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
      !ParseSyncParamsFromCommandLine(command_line, &sync_params)) {
    PrintUsage(argv[0]);
    return -1;
  }

  async::Loop loop(&kAsyncLoopConfigAttachToThread);
  ledger::ConvergenceBenchmark app(&loop, entry_count, value_size, device_count,
                                   std::move(sync_params));
  return ledger::RunWithTracing(&loop, [&app] { app.Run(); });
}
