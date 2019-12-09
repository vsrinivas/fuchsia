// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/time.h>

#include <iostream>
#include <memory>
#include <set>

#include <trace/event.h>

#include "peridot/lib/rng/test_random.h"
#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/platform/platform.h"
#include "src/ledger/bin/platform/scoped_tmp_dir.h"
#include "src/ledger/bin/testing/data_generator.h"
#include "src/ledger/bin/testing/get_ledger.h"
#include "src/ledger/bin/testing/get_page_ensure_initialized.h"
#include "src/ledger/bin/testing/ledger_memory_usage.h"
#include "src/ledger/bin/testing/page_data_generator.h"
#include "src/ledger/bin/testing/quit_on_error.h"
#include "src/ledger/bin/testing/run_with_tracing.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/vmo/strings.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "third_party/abseil-cpp/absl/flags/flag.h"
#include "third_party/abseil-cpp/absl/flags/parse.h"
#include "third_party/abseil-cpp/absl/strings/numbers.h"
#include "third_party/abseil-cpp/absl/strings/str_cat.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

ABSL_FLAG(ssize_t, entry_count, -1, "number of entries to delete");
ABSL_FLAG(ssize_t, transaction_size, -1, "number of element in the transaction");
ABSL_FLAG(ssize_t, key_size, -1, "size of the keys of entries");
ABSL_FLAG(ssize_t, value_size, -1, "size of the values of entries");
ABSL_FLAG(bool, refs, false,
          "the reference strategy: true if every value is inserted as a reference, false if every "
          "value is inserted as a FIDL array");
ABSL_FLAG(bool, update, false,
          "whether operations will update existing entries (put with existing keys and new values");
ABSL_FLAG(int, seed, 0, "(optional) the seed for key and value generation");

namespace ledger {
namespace {

// Benchmark that measures performance of the Put() operation.
//
// Parameters:
//   --entry-count=<int> the number of entries to be put
//   --transaction-size=<int> the size of a single transaction in number of put
//     operations. If equal to 0, no explicit transactions will be made.
//   --key-size=<int> the size of a single key in bytes
//   --value-size=<int> the size of a single value in bytes
//   --refs=(on|off) the reference strategy: on if every value is inserted
//     as a reference, off if every value is inserted as a FIDL array.
//   --update whether operations will update existing entries (put with existing
//     keys and new values)
//   --seed=<int> (optional) the seed for key and value generation
class PutBenchmark : public PageWatcher {
 public:
  PutBenchmark(async::Loop* loop, std::unique_ptr<sys::ComponentContext> component_context,
               int entry_count, int transaction_size, int key_size, int value_size, bool update,
               PageDataGenerator::ReferenceStrategy reference_strategy, uint64_t seed);
  PutBenchmark(const PutBenchmark&) = delete;
  PutBenchmark& operator=(const PutBenchmark&) = delete;

  void Run();

  // PageWatcher:
  void OnChange(PageChange page_change, ResultState result_state,
                OnChangeCallback callback) override;

 private:
  // Initilizes the keys to be used in the benchmark. In case the benchmark is
  // on updating entries, it also adds these keys in the ledger with some
  // initial values.
  void InitializeKeys(fit::function<void(std::vector<std::vector<uint8_t>>)> on_done);

  void BindWatcher(std::vector<std::vector<uint8_t>> keys);
  void RunSingle(int i, std::vector<std::vector<uint8_t>> keys);
  void CommitAndRunNext(int i, size_t key_number, std::vector<std::vector<uint8_t>> keys);
  void PutEntry(std::vector<uint8_t> key, std::vector<uint8_t> value,
                fit::function<void()> on_done);

  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  rng::TestRandom random_;
  DataGenerator generator_;
  PageDataGenerator page_data_generator_;

  std::unique_ptr<sys::ComponentContext> component_context_;
  std::unique_ptr<Platform> platform_;
  std::unique_ptr<ScopedTmpDir> tmp_dir_;
  const int entry_count_;
  const int transaction_size_;
  const int key_size_;
  const int value_size_;
  const bool update_;

  fidl::Binding<PageWatcher> page_watcher_binding_;
  const PageDataGenerator::ReferenceStrategy reference_strategy_;

  fuchsia::sys::ComponentControllerPtr component_controller_;
  LedgerPtr ledger_;
  PagePtr page_;
  // Keys that we use to identify a change event. For transaction_size = 1 it
  // contains all the keys, otherwise only the last changed key for each
  // transaction.
  std::set<size_t> keys_to_receive_;
  // Whether all Put operations have terminated. Shut down should be blocked
  // until this is set to true.
  bool insertions_finished_ = false;
  // Whether all expected watch notifications have been received. Shut down
  // should be blocked until this is set to true.
  bool all_watcher_notifications_received_ = false;
  LedgerMemoryEstimator memory_estimator_;
};

constexpr absl::string_view kStoragePath = "/data/benchmark/ledger/put";

PutBenchmark::PutBenchmark(async::Loop* loop,
                           std::unique_ptr<sys::ComponentContext> component_context,
                           int entry_count, int transaction_size, int key_size, int value_size,
                           bool update, PageDataGenerator::ReferenceStrategy reference_strategy,
                           uint64_t seed)
    : loop_(loop),
      random_(seed),
      generator_(&random_),
      page_data_generator_(&random_),
      component_context_(std::move(component_context)),
      platform_(MakePlatform()),
      tmp_dir_(platform_->file_system()->CreateScopedTmpDir(
          DetachedPath(convert::ToString(kStoragePath)))),
      entry_count_(entry_count),
      transaction_size_(transaction_size),
      key_size_(key_size),
      value_size_(value_size),
      update_(update),
      page_watcher_binding_(this),
      reference_strategy_(reference_strategy) {
  FXL_DCHECK(loop_);
  FXL_DCHECK(entry_count > 0);
  FXL_DCHECK(transaction_size >= 0);
  FXL_DCHECK(key_size > 0);
  FXL_DCHECK(value_size > 0);
}

void PutBenchmark::Run() {
  FXL_LOG(INFO) << "--" << FLAGS_entry_count.Name() << "=" << entry_count_             //
                << " --" << FLAGS_transaction_size.Name() << "=" << transaction_size_  //
                << " --" << FLAGS_key_size.Name() << "=" << key_size_                  //
                << " --" << FLAGS_value_size.Name() << "=" << value_size_              //
                << " --" << FLAGS_refs.Name() << "="
                << (reference_strategy_ == PageDataGenerator::ReferenceStrategy::INLINE ? "false"
                                                                                        : "true")
                << (update_ ? absl::StrCat(" --", FLAGS_update.Name()) : "");
  Status status =
      GetLedger(component_context_.get(), component_controller_.NewRequest(), nullptr, "", "put",
                tmp_dir_->path(), QuitLoopClosure(), &ledger_, kDefaultGarbageCollectionPolicy);
  if (QuitOnError(QuitLoopClosure(), status, "GetLedger")) {
    return;
  }
  FXL_CHECK(memory_estimator_.Init());

  GetPageEnsureInitialized(
      &ledger_, nullptr, DelayCallback::YES, QuitLoopClosure(),
      [this](Status status, PagePtr page, PageId id) mutable {
        if (QuitOnError(QuitLoopClosure(), status, "GetPageEnsureInitialized")) {
          return;
        }
        page_ = std::move(page);

        InitializeKeys([this](std::vector<std::vector<uint8_t>> keys) mutable {
          if (transaction_size_ > 0) {
            page_->StartTransaction();
            TRACE_ASYNC_BEGIN("benchmark", "transaction", 0);
          }
          BindWatcher(std::move(keys));
        });
      });
}

void PutBenchmark::OnChange(PageChange page_change, ResultState /*result_state*/,
                            OnChangeCallback callback) {
  for (auto const& change : page_change.changed_entries) {
    size_t key_number = generator_.GetKeyId(change.key);
    if (keys_to_receive_.find(key_number) != keys_to_receive_.end()) {
      TRACE_ASYNC_END("benchmark", "local_change_notification", key_number);
      keys_to_receive_.erase(key_number);
    }
  }
  if (keys_to_receive_.empty()) {
    all_watcher_notifications_received_ = true;
    // All watcher notifications have been received, waiting for put operations
    // to finish before shutting down.
    if (insertions_finished_) {
      ShutDown();
    }
  }
  callback(nullptr);
}

void PutBenchmark::InitializeKeys(fit::function<void(std::vector<std::vector<uint8_t>>)> on_done) {
  std::vector<std::vector<uint8_t>> keys =
      generator_.MakeKeys(entry_count_, key_size_, entry_count_);
  std::vector<std::vector<uint8_t>> keys_cloned;
  for (int i = 0; i < entry_count_; ++i) {
    keys_cloned.push_back(keys[i]);
    if (transaction_size_ == 0 || i % transaction_size_ == transaction_size_ - 1) {
      keys_to_receive_.insert(generator_.GetKeyId(keys[i]));
    }
  }
  // Last key should always be recorded so the last transaction is not lost.
  size_t last_key_number = generator_.GetKeyId(keys.back());
  keys_to_receive_.insert(last_key_number);
  if (!update_) {
    on_done(std::move(keys));
    return;
  }
  page_data_generator_.Populate(
      &page_, std::move(keys_cloned), value_size_, entry_count_, reference_strategy_,
      Priority::EAGER,
      [this, keys = std::move(keys), on_done = std::move(on_done)](Status status) mutable {
        if (QuitOnError(QuitLoopClosure(), status, "PageDataGenerator::Populate")) {
          return;
        }
        on_done(std::move(keys));
      });
}

void PutBenchmark::BindWatcher(std::vector<std::vector<uint8_t>> keys) {
  PageSnapshotPtr snapshot;
  page_->GetSnapshot(snapshot.NewRequest(), {}, page_watcher_binding_.NewBinding());
  page_->Sync([this, keys = std::move(keys)]() mutable { RunSingle(0, std::move(keys)); });
}

void PutBenchmark::RunSingle(int i, std::vector<std::vector<uint8_t>> keys) {
  if (i == entry_count_) {
    insertions_finished_ = true;
    // All sent, waiting for watcher notifications before shutting down.
    if (all_watcher_notifications_received_) {
      ShutDown();
    }
    return;
  }

  std::vector<uint8_t> value = generator_.MakeValue(value_size_);
  size_t key_number = generator_.GetKeyId(keys[i]);
  if (transaction_size_ == 0) {
    TRACE_ASYNC_BEGIN("benchmark", "local_change_notification", key_number);
  }
  PutEntry(std::move(keys[i]), std::move(value),
           [this, i, key_number, keys = std::move(keys)]() mutable {
             uint64_t memory;
             FXL_CHECK(memory_estimator_.GetLedgerMemoryUsage(&memory));
             TRACE_COUNTER("benchmark", "ledger_memory_put", i, "memory", TA_UINT64(memory));
             if (transaction_size_ > 0 &&
                 (i % transaction_size_ == transaction_size_ - 1 || i + 1 == entry_count_)) {
               CommitAndRunNext(i, key_number, std::move(keys));
             } else {
               RunSingle(i + 1, std::move(keys));
             }
           });
}

void PutBenchmark::PutEntry(std::vector<uint8_t> key, std::vector<uint8_t> value,
                            fit::function<void()> on_done) {
  auto trace_event_id = TRACE_NONCE();
  TRACE_ASYNC_BEGIN("benchmark", "put", trace_event_id);
  if (reference_strategy_ == PageDataGenerator::ReferenceStrategy::INLINE) {
    page_->Put(std::move(key), std::move(value));
    page_->Sync([trace_event_id, on_done = std::move(on_done)] {
      TRACE_ASYNC_END("benchmark", "put", trace_event_id);
      on_done();
    });
    return;
  }
  ledger::SizedVmo vmo;
  FXL_CHECK(ledger::VmoFromString(convert::ToStringView(value), &vmo));
  TRACE_ASYNC_BEGIN("benchmark", "create_reference", trace_event_id);
  page_->CreateReferenceFromBuffer(
      std::move(vmo).ToTransport(),
      [this, trace_event_id, key = std::move(key), on_done = std::move(on_done)](
          fuchsia::ledger::Page_CreateReferenceFromBuffer_Result result) mutable {
        if (QuitOnError(QuitLoopClosure(), result, "Page::CreateReferenceFromBuffer")) {
          return;
        }
        TRACE_ASYNC_END("benchmark", "create_reference", trace_event_id);
        TRACE_ASYNC_BEGIN("benchmark", "put_reference", trace_event_id);
        page_->PutReference(std::move(key), std::move(result.response().reference),
                            Priority::EAGER);
        page_->Sync([trace_event_id, on_done = std::move(on_done)] {
          TRACE_ASYNC_END("benchmark", "put_reference", trace_event_id);
          TRACE_ASYNC_END("benchmark", "put", trace_event_id);
          on_done();
        });
      });
}

void PutBenchmark::CommitAndRunNext(int i, size_t key_number,
                                    std::vector<std::vector<uint8_t>> keys) {
  TRACE_ASYNC_BEGIN("benchmark", "local_change_notification", key_number);
  TRACE_ASYNC_BEGIN("benchmark", "commit", i / transaction_size_);
  page_->Commit();
  page_->Sync([this, i, keys = std::move(keys)]() mutable {
    TRACE_ASYNC_END("benchmark", "commit", i / transaction_size_);
    TRACE_ASYNC_END("benchmark", "transaction", i / transaction_size_);

    if (i == entry_count_ - 1) {
      RunSingle(i + 1, std::move(keys));
      return;
    }
    page_->StartTransaction();
    page_->Sync([this, i = i + 1, keys = std::move(keys)]() mutable {
      TRACE_ASYNC_BEGIN("benchmark", "transaction", i / transaction_size_);
      RunSingle(i, std::move(keys));
    });
  });
}

void PutBenchmark::ShutDown() {
  // Shut down the Ledger process first as it relies on |tmp_dir_| storage.
  KillLedgerProcess(&component_controller_);
  loop_->Quit();
}

fit::closure PutBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

int Main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::Create();

  ssize_t entry_count = absl::GetFlag(FLAGS_entry_count);
  ssize_t transaction_size = absl::GetFlag(FLAGS_transaction_size);
  ssize_t key_size = absl::GetFlag(FLAGS_key_size);
  ssize_t value_size = absl::GetFlag(FLAGS_value_size);
  bool update = absl::GetFlag(FLAGS_update);
  bool refs_flag = absl::GetFlag(FLAGS_refs);
  int seed = absl::GetFlag(FLAGS_seed);

  PageDataGenerator::ReferenceStrategy ref_strategy;
  if (refs_flag) {
    ref_strategy = PageDataGenerator::ReferenceStrategy::REFERENCE;
  } else {
    ref_strategy = PageDataGenerator::ReferenceStrategy::INLINE;
  }

  if (entry_count <= 0 || transaction_size < 0 || key_size <= 0 || value_size <= 0) {
    std::cerr << "Incorrect parameter values" << std::endl;
    return 1;
  }

  PutBenchmark app(&loop, std::move(component_context), entry_count, transaction_size, key_size,
                   value_size, update, ref_strategy, seed);

  return RunWithTracing(&loop, [&app] { app.Run(); });
}

}  // namespace
}  // namespace ledger

int main(int argc, char** argv) { return ledger::Main(argc, argv); }
