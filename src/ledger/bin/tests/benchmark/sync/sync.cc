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

constexpr fxl::StringView kBinaryPath = "fuchsia-pkg://fuchsia.com/ledger_benchmarks#meta/sync.cmx";
constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/sync";
constexpr fxl::StringView kChangeCountFlag = "change-count";
constexpr fxl::StringView kValueSizeFlag = "value-size";
constexpr fxl::StringView kEntriesPerChangeFlag = "entries-per-change";
constexpr fxl::StringView kRefsFlag = "refs";

constexpr fxl::StringView kRefsOnFlag = "on";
constexpr fxl::StringView kRefsOffFlag = "off";

constexpr size_t kKeySize = 100;

void PrintUsage() {
  std::cout << "Usage: trace record "
            << kBinaryPath
            // Comment to make clang format not break formatting.
            << " --" << kChangeCountFlag << "=<int>"
            << " --" << kValueSizeFlag << "=<int>"
            << " --" << kEntriesPerChangeFlag << "=<int>"
            << " --" << kRefsFlag << "=(" << kRefsOnFlag << "|" << kRefsOffFlag << ")"
            << GetSyncParamsUsage() << std::endl;
}

// Benchmark that measures sync latency between two Ledger instances syncing
// through the cloud. This emulates syncing between devices, as the Ledger
// instances have separate disk storage.
//
// Cloud sync needs to be configured on the device in order for the benchmark to
// run.
//
// Parameters:
//   --change-count=<int> the number of changes to be made to the page (each
//   change is done as transaction and can include several put operations).
//   --value-size=<int> the size of a single value in bytes
//   --entries-per-change=<int> number of entries added in the transaction
//   --refs=(on|off) reference strategy: on to put values as references, off to
//     put them as FIDL arrays.
//   --credentials-path=<file path> Firestore service account credentials
class SyncBenchmark : public PageWatcher {
 public:
  SyncBenchmark(async::Loop* loop, std::unique_ptr<sys::ComponentContext> component_context,
                size_t change_count, size_t value_size, size_t entries_per_change,
                PageDataGenerator::ReferenceStrategy reference_strategy, SyncParams sync_params);

  void Run();

  // PageWatcher:
  void OnChange(PageChange page_change, ResultState result_state,
                OnChangeCallback callback) override;

 private:
  void RunSingleChange(size_t change_number);

  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  rng::TestRandom random_;
  DataGenerator generator_;
  PageDataGenerator page_data_generator_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  cloud_provider_firestore::CloudProviderFactory cloud_provider_factory_;
  const size_t change_count_;
  const size_t value_size_;
  const size_t entries_per_change_;
  const PageDataGenerator::ReferenceStrategy reference_strategy_;
  const cloud_provider_firestore::CloudProviderFactory::UserId user_id_;
  fidl::Binding<PageWatcher> page_watcher_binding_;
  files::ScopedTempDir alpha_tmp_dir_;
  files::ScopedTempDir beta_tmp_dir_;
  fuchsia::sys::ComponentControllerPtr alpha_controller_;
  fuchsia::sys::ComponentControllerPtr beta_controller_;
  LedgerPtr alpha_;
  LedgerPtr beta_;
  PageId page_id_;
  PagePtr alpha_page_;
  PagePtr beta_page_;

  size_t changed_entries_received_;

  FXL_DISALLOW_COPY_AND_ASSIGN(SyncBenchmark);
};

SyncBenchmark::SyncBenchmark(async::Loop* loop,
                             std::unique_ptr<sys::ComponentContext> component_context,
                             size_t change_count, size_t value_size, size_t entries_per_change,
                             PageDataGenerator::ReferenceStrategy reference_strategy,
                             SyncParams sync_params)
    : loop_(loop),
      random_(0),
      generator_(&random_),
      page_data_generator_(&random_),
      component_context_(std::move(component_context)),
      cloud_provider_factory_(component_context_.get(), &random_, std::move(sync_params.api_key),
                              std::move(sync_params.credentials)),
      change_count_(change_count),
      value_size_(value_size),
      entries_per_change_(entries_per_change),
      reference_strategy_(reference_strategy),
      user_id_(cloud_provider_firestore::CloudProviderFactory::UserId::New()),
      page_watcher_binding_(this),
      alpha_tmp_dir_(kStoragePath),
      beta_tmp_dir_(kStoragePath) {
  FXL_DCHECK(loop_);
  FXL_DCHECK(change_count_ > 0);
  FXL_DCHECK(value_size_ > 0);
  FXL_DCHECK(entries_per_change_ > 0);
  cloud_provider_factory_.Init();
}

void SyncBenchmark::Run() {
  // Name of the storage directory currently identifies the user. Ensure the
  // most nested directory has the same name to make the ledgers sync.
  std::string alpha_path = alpha_tmp_dir_.path() + "/sync_user";
  bool ret = files::CreateDirectory(alpha_path);
  FXL_DCHECK(ret);

  std::string beta_path = beta_tmp_dir_.path() + "/sync_user";
  ret = files::CreateDirectory(beta_path);
  FXL_DCHECK(ret);

  cloud_provider::CloudProviderPtr cloud_provider_alpha;
  cloud_provider_factory_.MakeCloudProvider(user_id_, cloud_provider_alpha.NewRequest());
  Status status = GetLedger(component_context_.get(), alpha_controller_.NewRequest(),
                            std::move(cloud_provider_alpha), user_id_.user_id(), "sync",
                            DetachedPath(std::move(alpha_path)), QuitLoopClosure(), &alpha_,
                            kDefaultGarbageCollectionPolicy);
  if (QuitOnError(QuitLoopClosure(), status, "alpha ledger")) {
    return;
  };

  cloud_provider::CloudProviderPtr cloud_provider_beta;
  cloud_provider_factory_.MakeCloudProvider(user_id_, cloud_provider_beta.NewRequest());

  status =
      GetLedger(component_context_.get(), beta_controller_.NewRequest(),
                std::move(cloud_provider_beta), user_id_.user_id(), "sync", DetachedPath(beta_path),
                QuitLoopClosure(), &beta_, kDefaultGarbageCollectionPolicy);
  if (QuitOnError(QuitLoopClosure(), status, "beta ledger")) {
    return;
  }
  GetPageEnsureInitialized(
      &alpha_, nullptr, DelayCallback::YES, QuitLoopClosure(),
      [this](Status status, PagePtr page, PageId id) {
        if (QuitOnError(QuitLoopClosure(), status, "alpha page initialization")) {
          return;
        }
        alpha_page_ = std::move(page);
        page_id_ = id;
        beta_->GetPage(fidl::MakeOptional(id), beta_page_.NewRequest());
        PageSnapshotPtr snapshot;
        beta_page_->GetSnapshot(snapshot.NewRequest(), {}, page_watcher_binding_.NewBinding());
        beta_page_->Sync([this] { RunSingleChange(0); });
      });
}

void SyncBenchmark::OnChange(PageChange page_change, ResultState result_state,
                             OnChangeCallback callback) {
  FXL_DCHECK(!page_change.changed_entries.empty());
  size_t i = generator_.GetKeyId(page_change.changed_entries.at(0).key);
  changed_entries_received_ += page_change.changed_entries.size();
  if (result_state == ResultState::COMPLETED || result_state == ResultState::PARTIAL_STARTED) {
    TRACE_ASYNC_END("benchmark", "sync latency", i);
  }
  if (result_state == ResultState::COMPLETED || result_state == ResultState::PARTIAL_COMPLETED) {
    FXL_DCHECK(changed_entries_received_ == entries_per_change_);
    RunSingleChange(i + 1);
  }
  callback(nullptr);
}

void SyncBenchmark::RunSingleChange(size_t change_number) {
  if (change_number == change_count_) {
    ShutDown();
    return;
  }

  std::vector<std::vector<uint8_t>> keys(entries_per_change_);
  for (size_t i = 0; i < entries_per_change_; i++) {
    // Keys are distinct, but they all have the same id (|change_number|), which
    // will be used to end the trace.
    keys[i] = generator_.MakeKey(change_number, kKeySize);
  }

  changed_entries_received_ = 0;
  TRACE_ASYNC_BEGIN("benchmark", "sync latency", change_number);
  page_data_generator_.Populate(
      &alpha_page_, std::move(keys), value_size_, entries_per_change_, reference_strategy_,
      Priority::EAGER, [this](Status status) {
        if (QuitOnError(QuitLoopClosure(), status, "PageDataGenerator::Populate")) {
          return;
        }
      });
}

void SyncBenchmark::ShutDown() {
  KillLedgerProcess(&alpha_controller_);
  KillLedgerProcess(&beta_controller_);
  loop_->Quit();
}

fit::closure SyncBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

int Main(int argc, const char** argv) {
  fxl::CommandLine command_line = fxl::CommandLineFromArgcArgv(argc, argv);
  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::Create();

  std::string change_count_str;
  size_t change_count;
  std::string value_size_str;
  size_t value_size;
  std::string entries_per_change_str;
  size_t entries_per_change;
  std::string reference_strategy_str;
  SyncParams sync_params;
  if (!command_line.GetOptionValue(kChangeCountFlag.ToString(), &change_count_str) ||
      !fxl::StringToNumberWithError(change_count_str, &change_count) || change_count <= 0 ||
      !command_line.GetOptionValue(kValueSizeFlag.ToString(), &value_size_str) ||
      !fxl::StringToNumberWithError(value_size_str, &value_size) || value_size <= 0 ||
      !command_line.GetOptionValue(kEntriesPerChangeFlag.ToString(), &entries_per_change_str) ||
      !fxl::StringToNumberWithError(entries_per_change_str, &entries_per_change) ||
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

  SyncBenchmark app(&loop, std::move(component_context), change_count, value_size,
                    entries_per_change, reference_strategy, std::move(sync_params));
  return RunWithTracing(&loop, [&app] { app.Run(); });
}

}  // namespace
}  // namespace ledger

int main(int argc, const char** argv) { return ledger::Main(argc, argv); }
