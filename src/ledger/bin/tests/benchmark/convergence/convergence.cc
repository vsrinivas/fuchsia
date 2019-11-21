// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl/cpp/optional.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/time.h>

#include <iostream>
#include <memory>
#include <set>
#include <vector>

#include <trace/event.h>

#include "peridot/lib/rng/test_random.h"
#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/testing/data_generator.h"
#include "src/ledger/bin/testing/get_ledger.h"
#include "src/ledger/bin/testing/get_page_ensure_initialized.h"
#include "src/ledger/bin/testing/quit_on_error.h"
#include "src/ledger/bin/testing/run_with_tracing.h"
#include "src/ledger/bin/testing/sync_params.h"
#include "src/ledger/cloud_provider_firestore/bin/testing/cloud_provider_factory.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/file.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/command_line.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "src/lib/fxl/strings/string_number_conversions.h"

namespace ledger {
namespace {
constexpr fxl::StringView kBinaryPath =
    "fuchsia-pkg://fuchsia.com/ledger_benchmarks#meta/convergence.cmx";
constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/convergence";
constexpr fxl::StringView kEntryCountFlag = "entry-count";
constexpr fxl::StringView kValueSizeFlag = "value-size";
constexpr fxl::StringView kDeviceCountFlag = "device-count";

void PrintUsage() {
  std::cout << "Usage: trace record "
            << kBinaryPath
            // Comment to make clang format not break formatting.
            << " --" << kEntryCountFlag << "=<int>"
            << " --" << kValueSizeFlag << "=<int>"
            << " --" << kDeviceCountFlag << "=<int>" << GetSyncParamsUsage() << std::endl;
}

constexpr size_t kKeySize = 100;

// Benchmark that measures the time it takes to sync and reconcile concurrent
// writes.
//
// In this scenario there are specified number of (emulated) devices. At each
// step, every device makes a concurrent write, and we measure the time until
// all the changes are visible to all devices.
//
// Parameters:
//   --entry-count=<int> the number of entries to be put by each device
//   --value-size=<int> the size of a single value in bytes
//   --device-count=<int> number of devices writing to the same page
//   --credentials-path=<file path> Firestore service account credentials
class ConvergenceBenchmark : public PageWatcher {
 public:
  ConvergenceBenchmark(async::Loop* loop, std::unique_ptr<sys::ComponentContext> component_context,
                       int entry_count, int value_size, int device_count, SyncParams sync_params);

  void Run();

  // PageWatcher:
  void OnChange(PageChange page_change, ResultState result_state,
                OnChangeCallback callback) override;

 private:
  // Instances needed to control the Ledger process associated with a device and
  // interact with it.
  struct DeviceContext {
    std::unique_ptr<files::ScopedTempDir> storage_directory;
    fuchsia::sys::ComponentControllerPtr controller;
    LedgerPtr ledger;
    PagePtr page_connection;
    std::unique_ptr<fidl::Binding<PageWatcher>> page_watcher;
  };

  void Start(int step);

  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  rng::TestRandom random_;
  DataGenerator generator_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  cloud_provider_firestore::CloudProviderFactory cloud_provider_factory_;
  const int entry_count_;
  const int value_size_;
  const int device_count_;
  const cloud_provider_firestore::CloudProviderFactory::UserId user_id_;
  // Track all Ledger instances running for this test and allow to interact with
  // it.
  std::vector<std::unique_ptr<DeviceContext>> devices_;
  PageId page_id_;
  std::multiset<std::string> remaining_keys_;
  int current_step_ = -1;

  FXL_DISALLOW_COPY_AND_ASSIGN(ConvergenceBenchmark);
};

ConvergenceBenchmark::ConvergenceBenchmark(async::Loop* loop,
                                           std::unique_ptr<sys::ComponentContext> component_context,
                                           int entry_count, int value_size, int device_count,
                                           SyncParams sync_params)
    : loop_(loop),
      random_(0),
      generator_(&random_),
      component_context_(std::move(component_context)),
      cloud_provider_factory_(component_context_.get(), &random_, std::move(sync_params.api_key),
                              std::move(sync_params.credentials)),
      entry_count_(entry_count),
      value_size_(value_size),
      device_count_(device_count),
      user_id_(cloud_provider_firestore::CloudProviderFactory::UserId::New()),
      devices_(device_count) {
  FXL_DCHECK(loop_);
  FXL_DCHECK(entry_count_ > 0);
  FXL_DCHECK(value_size_ > 0);
  FXL_DCHECK(device_count_ > 1);
  for (auto& device_context : devices_) {
    device_context = std::make_unique<DeviceContext>();
    device_context->storage_directory = std::make_unique<files::ScopedTempDir>(kStoragePath);
    device_context->page_watcher = std::make_unique<fidl::Binding<PageWatcher>>(this);
  }
  page_id_ = generator_.MakePageId();
  cloud_provider_factory_.Init();
}

void ConvergenceBenchmark::Run() {
  auto waiter = fxl::MakeRefCounted<callback::CompletionWaiter>();
  for (auto& device_context : devices_) {
    // Initialize ledgers in different paths to emulate separate devices,
    // but with the same lowest-level directory name, so they correspond to the
    // same "user".
    std::string synced_dir_path = device_context->storage_directory->path() + "/convergence_user";
    bool ret = files::CreateDirectory(synced_dir_path);
    FXL_DCHECK(ret);

    cloud_provider::CloudProviderPtr cloud_provider;
    cloud_provider_factory_.MakeCloudProvider(user_id_, cloud_provider.NewRequest());

    Status status = GetLedger(component_context_.get(), device_context->controller.NewRequest(),
                              std::move(cloud_provider), user_id_.user_id(), "convergence",
                              DetachedPath(synced_dir_path), QuitLoopClosure(),
                              &device_context->ledger, kDefaultGarbageCollectionPolicy);
    if (QuitOnError(QuitLoopClosure(), status, "GetLedger")) {
      return;
    }
    device_context->ledger->GetPage(fidl::MakeOptional(page_id_),
                                    device_context->page_connection.NewRequest());
    PageSnapshotPtr snapshot;
    // Register a watcher; we don't really need the snapshot.
    device_context->page_connection->GetSnapshot(snapshot.NewRequest(), {},
                                                 device_context->page_watcher->NewBinding());
    device_context->page_connection->Sync(waiter->NewCallback());
  }
  waiter->Finalize([this] { Start(0); });
}

void ConvergenceBenchmark::Start(int step) {
  if (step == entry_count_) {
    ShutDown();
    return;
  }

  for (int device_id = 0; device_id < device_count_; device_id++) {
    std::vector<uint8_t> key = generator_.MakeKey(device_count_ * step + device_id, kKeySize);
    // Insert each key N times, as we will receive N notifications - one for
    // each connection, sender included.
    for (int receiving_device = 0; receiving_device < device_count_; receiving_device++) {
      remaining_keys_.insert(convert::ToString(key));
    }
    std::vector<uint8_t> value = generator_.MakeValue(value_size_);
    devices_[device_id]->page_connection->Put(std::move(key), std::move(value));
  }

  TRACE_ASYNC_BEGIN("benchmark", "convergence", step);
  // Persist the current step, so that we know which dispatcher event to end in
  // OnChange().
  current_step_ = step;
}

void ConvergenceBenchmark::OnChange(PageChange page_change, ResultState result_state,
                                    OnChangeCallback callback) {
  FXL_DCHECK(result_state == ResultState::COMPLETED);
  for (auto& change : page_change.changed_entries) {
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

int Main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::Create();

  std::string entry_count_str;
  int entry_count;
  std::string value_size_str;
  int value_size;
  std::string device_count_str;
  int device_count;
  SyncParams sync_params;
  if (!command_line.GetOptionValue(kEntryCountFlag.ToString(), &entry_count_str) ||
      !fxl::StringToNumberWithError(entry_count_str, &entry_count) || entry_count <= 0 ||
      !command_line.GetOptionValue(kValueSizeFlag.ToString(), &value_size_str) ||
      !fxl::StringToNumberWithError(value_size_str, &value_size) || value_size <= 0 ||
      !command_line.GetOptionValue(kDeviceCountFlag.ToString(), &device_count_str) ||
      !fxl::StringToNumberWithError(device_count_str, &device_count) || device_count <= 0 ||
      !ParseSyncParamsFromCommandLine(command_line, component_context.get(), &sync_params)) {
    PrintUsage();
    return -1;
  }

  ConvergenceBenchmark app(&loop, std::move(component_context), entry_count, value_size,
                           device_count, std::move(sync_params));
  return RunWithTracing(&loop, [&app] { app.Run(); });
}

}  // namespace
}  // namespace ledger

int main(int argc, const char** argv) { return ledger::Main(argc, argv); }
