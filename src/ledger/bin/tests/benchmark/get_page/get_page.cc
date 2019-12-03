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
#include "src/lib/callback/trace_callback.h"
#include "src/lib/callback/waiter.h"
#include "src/lib/files/directory.h"
#include "src/lib/files/scoped_temp_dir.h"
#include "src/lib/fxl/logging.h"
#include "src/lib/fxl/memory/ref_ptr.h"
#include "third_party/abseil-cpp/absl/flags/flag.h"
#include "third_party/abseil-cpp/absl/flags/parse.h"
#include "third_party/abseil-cpp/absl/strings/numbers.h"

ABSL_FLAG(ssize_t, requests_count, -1, "number of request to GetPage");
ABSL_FLAG(bool, reuse, false,
          "if this flag is specified, the same id will be used. Otherwise, a new page with a "
          "random id is requested every time");
ABSL_FLAG(bool, wait_for_cached_page, false,
          "if this flag is specified, the benchmark will wait for a sufficient amount of time "
          "before each page request, to allow Ledger to precache an empty new page");
ABSL_FLAG(bool, clear_pages, false,
          "if this flag is specified, the benchmark will clear the page each time");

namespace ledger {
namespace {

constexpr fxl::StringView kStoragePath = "/data/benchmark/ledger/get_page";

// The delay to be used when waiting for a ledger background I/O operation to
// finish. This is used when it is not possible to wait for a specific event,
// like in the case of expecting the precached Page to be ready at the time of
// Page request. 500ms is chosen as a sufficiently long delay to guarantee this.
constexpr zx::duration kDelay = zx::msec(500);
constexpr size_t kKeySize = 10;
constexpr size_t kValueSize = 10;

// Benchmark that measures the time taken to get a page.
//
// Parameters:
//   --requests-count=<int> number of requests made.
//   --reuse - if this flag is specified, the same id will be used. Otherwise, a
//   new page with a random id is requested every time.
//   --wait_for_cached_page - if this flag is specified, the benchmark will wait
//   for a sufficient amount of time before each page request, to allow Ledger
//   to precache an empty new page.
class GetPageBenchmark {
 public:
  GetPageBenchmark(async::Loop* loop, std::unique_ptr<sys::ComponentContext> component_context,
                   size_t requests_count, bool reuse, bool wait_for_cached_page, bool clear_pages);
  GetPageBenchmark(const GetPageBenchmark&) = delete;
  GetPageBenchmark& operator=(const GetPageBenchmark&) = delete;

  void Run();

 private:
  void RunSingle(size_t request_number);
  void PopulateAndClearPage(size_t page_index, fit::closure callback);
  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  rng::TestRandom random_;
  files::ScopedTempDir tmp_dir_;
  DataGenerator generator_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  const size_t requests_count_;
  const bool reuse_;
  const bool wait_for_cached_page_;
  const bool clear_pages_;
  fuchsia::sys::ComponentControllerPtr component_controller_;
  LedgerPtr ledger_;
  PageIdPtr page_id_;
  std::vector<PagePtr> pages_;
};

GetPageBenchmark::GetPageBenchmark(async::Loop* loop,
                                   std::unique_ptr<sys::ComponentContext> component_context,
                                   size_t requests_count, bool reuse, bool wait_for_cached_page,
                                   bool clear_pages)
    : loop_(loop),
      random_(0),
      tmp_dir_(kStoragePath),
      generator_(&random_),
      component_context_(std::move(component_context)),
      requests_count_(requests_count),
      reuse_(reuse),
      wait_for_cached_page_(wait_for_cached_page),
      clear_pages_(clear_pages) {
  FXL_DCHECK(loop_);
  FXL_DCHECK(requests_count_ > 0);
  pages_.resize(requests_count_);
}

void GetPageBenchmark::Run() {
  Status status = GetLedger(component_context_.get(), component_controller_.NewRequest(), nullptr,
                            "", "get_page", DetachedPath(tmp_dir_.path()), QuitLoopClosure(),
                            &ledger_, kDefaultGarbageCollectionPolicy);

  if (QuitOnError(QuitLoopClosure(), status, "GetLedger")) {
    return;
  }

  page_id_ = fidl::MakeOptional(generator_.MakePageId());
  RunSingle(0);
}

void GetPageBenchmark::RunSingle(size_t request_number) {
  if (request_number == requests_count_) {
    ShutDown();
    return;
  }
  if (wait_for_cached_page_) {
    // Wait before each page request, so that a pre-cached page is ready.
    zx_nanosleep(zx_deadline_after(kDelay.get()));
  }

  auto waiter = fxl::MakeRefCounted<callback::CompletionWaiter>();
  TRACE_ASYNC_BEGIN("benchmark", "get_page", request_number);
  ledger_->GetPage(reuse_ ? fidl::Clone(page_id_) : nullptr, pages_[request_number].NewRequest());
  ledger_->Sync([this, callback = waiter->NewCallback(), request_number]() mutable {
    TRACE_ASYNC_END("benchmark", "get_page", request_number);
    if (!clear_pages_) {
      callback();
      return;
    }
    // Make sure there is something written on disk before clearing the
    // page. This will test the behavior of actually clearing a page (vs.
    // just closing an always empty page).
    PopulateAndClearPage(request_number, std::move(callback));
  });

  auto get_id_callback = TRACE_CALLBACK(waiter->NewCallback(), "benchmark", "get_page_id");
  // Request the page id without waiting for the GetPage callback to be called.
  pages_[request_number]->GetId(
      [callback = std::move(get_id_callback)](PageId found_page_id) { callback(); });

  // Wait for both GetPage and GetId to finish, before starting the next run.
  waiter->Finalize([this, request_number]() {
    if (clear_pages_) {
      // To evict the cleared pages we need to close them.
      pages_[request_number].Unbind();
    }
    RunSingle(request_number + 1);
  });
}

void GetPageBenchmark::PopulateAndClearPage(size_t page_index, fit::closure callback) {
  pages_[page_index]->Put(generator_.MakeKey(page_index, kKeySize),
                          generator_.MakeValue(kValueSize));
  pages_[page_index]->Clear();
  pages_[page_index]->Sync(std::move(callback));
}

void GetPageBenchmark::ShutDown() {
  if (clear_pages_) {
    // Wait sufficient amount of time so that all cleared pages are evicted.
    zx_nanosleep(zx_deadline_after(kDelay.get()));
  }
  KillLedgerProcess(&component_controller_);
  loop_->Quit();
}

fit::closure GetPageBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

int Main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::Create();

  ssize_t requests_count = absl::GetFlag(FLAGS_requests_count);
  bool reuse = absl::GetFlag(FLAGS_reuse);
  bool wait_for_cached_page = absl::GetFlag(FLAGS_wait_for_cached_page);
  bool clear_pages = absl::GetFlag(FLAGS_clear_pages);
  if (requests_count <= 0) {
    std::cerr << "Incorrect parameter values" << std::endl;
    return 1;
  }

  GetPageBenchmark app(&loop, std::move(component_context), requests_count, reuse,
                       wait_for_cached_page, clear_pages);

  return RunWithTracing(&loop, [&app] { app.Run(); });
}

}  // namespace
}  // namespace ledger

int main(int argc, char** argv) { return ledger::Main(argc, argv); }
