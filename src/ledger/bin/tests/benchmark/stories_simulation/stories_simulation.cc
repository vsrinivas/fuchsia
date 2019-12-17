// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/trace/event.h>
#include <lib/zx/time.h>

#include <iostream>
#include <list>
#include <memory>

#include "src/ledger/bin/app/flags.h"
#include "src/ledger/bin/fidl/include/types.h"
#include "src/ledger/bin/platform/ledger_memory_estimator.h"
#include "src/ledger/bin/platform/platform.h"
#include "src/ledger/bin/platform/scoped_tmp_location.h"
#include "src/ledger/bin/testing/data_generator.h"
#include "src/ledger/bin/testing/get_ledger.h"
#include "src/ledger/bin/testing/get_page_ensure_initialized.h"
#include "src/ledger/bin/testing/page_data_generator.h"
#include "src/ledger/bin/testing/quit_on_error.h"
#include "src/ledger/bin/testing/run_with_tracing.h"
#include "src/ledger/lib/callback/trace_callback.h"
#include "src/ledger/lib/callback/waiter.h"
#include "src/ledger/lib/convert/convert.h"
#include "src/ledger/lib/logging/logging.h"
#include "src/ledger/lib/memory/ref_ptr.h"
#include "src/ledger/lib/rng/test_random.h"
#include "src/ledger/lib/vmo/strings.h"
#include "third_party/abseil-cpp/absl/flags/flag.h"
#include "third_party/abseil-cpp/absl/flags/parse.h"
#include "third_party/abseil-cpp/absl/strings/numbers.h"
#include "third_party/abseil-cpp/absl/strings/str_format.h"
#include "third_party/abseil-cpp/absl/strings/string_view.h"

ABSL_FLAG(ssize_t, story_count, -1, "the number of stories to be created");
ABSL_FLAG(ssize_t, active_story_count, -1, "The number of active stories");
ABSL_FLAG(bool, wait_for_cached_page, false,
          "if this flag is specified, the benchmark will waitfor a sufficient amount of time "
          "before each page request, to allow Ledger to precache an empty new page");

namespace ledger {
namespace {
constexpr absl::string_view kMessageQueuePageId = "MessageQueuePage";
constexpr absl::string_view kAgentRunnerPageId = "AgentRunnerPage_";

// The delay to be used when waiting for ledger background I/O operations to finish. Adding this
// delay before creating a new story will simulate the optimal conditions for creating a new Story:
// A precached Page will be prepared on the background and, upon request, it will be attributed to
// the next Story, with minimal delay.
constexpr zx::duration kDelay = zx::msec(100);

// Contents and metadata as observed in the e2e tests.
constexpr size_t kStoryValueSize = 320;
constexpr size_t kLinkValueSize = 6766;
constexpr size_t kModuleValueSize = 7366;

// Returns the PageId for the i-th story created.
std::vector<uint8_t> GetStoryName(int i) {
  return convert::ToArray(
      absl::StrFormat("Story/Data/OpalStory28c2c54c-b35a-4edc-b012-1f%010d", i));
}

// Returns the DB key for the link created for the i-th story.
std::vector<uint8_t> GetLinkKey(int i) {
  return convert::ToArray(
      absl::StrFormat("fuchsia::modular::Link|3/OpalMod564ffe1c-3136-4103-a5a3-a2%010d/"
                      "card_data",
                      i));
}

// Returns the DB key for the module created for the i-th story.
std::vector<uint8_t> GetModuleKey(int i) {
  return convert::ToArray(absl::StrFormat("Module/OpalMod564ffe1c-3136-4103-a5a3-a2%010d", i));
}

std::unique_ptr<PageId> MakePageId(absl::string_view id) {
  PageId page_id;
  LEDGER_DCHECK(id.size() == page_id.id.size()) << id.size() << " != " << page_id.id.size();
  memcpy(page_id.id.data(), id.data(), id.length());
  return std::make_unique<PageId>(page_id);
}

fit::function<void(Status)> CheckStatusOKCallback() {
  return [](Status status) { LEDGER_CHECK(status == Status::OK); };
}

// Each story has 2 active connections (PagePtr) while being used, while a third one is opened to
// clear the page.
struct ActiveStory {
  PageId story_id;
  PagePtr connection1;
  PagePtr connection2;
  PagePtr connection_for_clear;
};

// A PageWatcher that doesn't read the contents of the given changes.
class EmptyWatcher : public PageWatcher {
 public:
  EmptyWatcher() : binding_(this) {}

  fidl::InterfaceHandle<PageWatcher> NewBinding() { return binding_.NewBinding(); }

 private:
  // PageWatcher:
  void OnChange(PageChange page_change, ResultState result_state,
                OnChangeCallback callback) override {
    callback(nullptr);
  }

  fidl::Binding<PageWatcher> binding_;
};

// Helper function for adding a watcher on a page.
void AddWatcher(const PagePtr* page, absl::string_view prefix, EmptyWatcher* watcher) {
  PageSnapshotPtr page_snapshot;
  (*page)->GetSnapshot(page_snapshot.NewRequest(), convert::ToArray(prefix),
                       (*watcher).NewBinding());
}

// Helper function for reading the entry with the given key from the page. The value is ignored.
void ReadFromPage(const PagePtr* page, const std::vector<uint8_t>& entry_key,
                  fit::function<void()> callback) {
  auto page_snapshot = std::make_unique<PageSnapshotPtr>();
  (*page)->GetSnapshot(page_snapshot->NewRequest(), convert::ToArray(""), nullptr);

  PageSnapshotPtr* page_snapshot_ptr = page_snapshot.get();
  (*page_snapshot_ptr)
      ->Get(entry_key, [page_snapshot = std::move(page_snapshot),
                        callback = std::move(callback)](auto /*result*/) { callback(); });
}

// Helper function for reading all entries with the given prefix from the page. The values are
// ignored.
void ReadAllFromPage(const PagePtr* page, const std::vector<uint8_t>& prefix,
                     fit::function<void()> callback) {
  auto page_snapshot = std::make_unique<PageSnapshotPtr>();
  (*page)->GetSnapshot(page_snapshot->NewRequest(), prefix, nullptr);
  PageSnapshotPtr* page_snapshot_ptr = page_snapshot.get();
  (*page_snapshot_ptr)
      ->GetEntries(convert::ToArray(""), nullptr,
                   [page_snapshot = std::move(page_snapshot), callback = std::move(callback)](
                       std::vector<Entry> entries, std::unique_ptr<Token> token) {
                     LEDGER_CHECK(entries.empty());
                     LEDGER_CHECK(token == nullptr);
                     callback();
                   });
}

// Benchmark that simulates story creating and removal.
//
// Parameters:
//   --story-count=<int> the number of stories to be created
//   --wait_for_cached_page - if this flag is specified, the benchmark will waitfor a sufficient
//   amount of time before each page request, to allow Ledger to precache an empty new page.
class StoriesBenchmark {
 public:
  StoriesBenchmark(async::Loop* loop, std::unique_ptr<sys::ComponentContext> component_context,
                   int story_count, int active_story_count, bool wait_for_cached);
  StoriesBenchmark(const StoriesBenchmark&) = delete;
  StoriesBenchmark& operator=(const StoriesBenchmark&) = delete;

  void Run();

 private:
  // Initializes the default pages, i.e. the root, message queue and agent runner pages.
  void InitializeDefaultPages();

  // Runs the |i|-th iteration of this page, i.e. creates the |i|-th story.
  void RunSingle(int i);

  // Adds contents on the |i|-th story's page.
  void EditStory(int i, PageId story_id, fit::function<void()> callback);

  // After the |i|-th story has been created, this method decides whether to perform a cleanup
  // operation or not.
  void MaybeCleanup(int i, fit::function<void()> callback);

  // Clears the page that was the |story_index|-th one to be created.
  void ClearLRUPage(int story_index, fit::function<void()> callback);

  // Clears all remaining pages from the list of active ones, starting by the |story_index|-th one
  // to be created.
  void ClearRemainingPages(size_t story_index, fit::function<void()> callback);

  void ShutDown();
  fit::closure QuitLoopClosure();

  async::Loop* const loop_;
  TestRandom random_;
  DataGenerator generator_;
  PageDataGenerator page_data_generator_;

  std::unique_ptr<Platform> platform_;
  std::unique_ptr<ScopedTmpLocation> tmp_location_;
  std::unique_ptr<sys::ComponentContext> component_context_;
  LedgerMemoryEstimator* memory_estimator_;

  // Input arguments.
  const size_t story_count_;
  const size_t active_story_count_;
  const bool wait_for_cached_page_;

  fuchsia::sys::ComponentControllerPtr component_controller_;
  LedgerPtr ledger_;

  // Pages kept active throughout Modular's execution.
  PagePtr root_page_;
  PagePtr message_queue_page_;
  PagePtr agent_runner_page_;

  // Watchers
  EmptyWatcher root_watcher_on_story_;
  EmptyWatcher root_watcher_on_focus_;
  EmptyWatcher message_queue_watcher_;
  EmptyWatcher agent_runner_watcher_;

  // The list of active stories. Newly created stories are appended at the end.
  std::list<ActiveStory> active_stories_;
  EmptyWatcher story_watcher1_;
  EmptyWatcher story_watcher2_;
};

StoriesBenchmark::StoriesBenchmark(async::Loop* loop,
                                   std::unique_ptr<sys::ComponentContext> component_context,
                                   int story_count, int active_story_count,
                                   bool wait_for_cached_page)
    : loop_(loop),
      random_(0),
      generator_(&random_),
      page_data_generator_(&random_),
      platform_(MakePlatform()),
      tmp_location_(platform_->file_system()->CreateScopedTmpLocation()),
      component_context_(std::move(component_context)),
      memory_estimator_(platform_->memory_estimator()),
      story_count_(story_count),
      active_story_count_(active_story_count),
      wait_for_cached_page_(wait_for_cached_page) {
  LEDGER_DCHECK(loop_);
  LEDGER_DCHECK(story_count > 0);
}

void StoriesBenchmark::Run() {
  Status status = GetLedger(component_context_.get(), component_controller_.NewRequest(), nullptr,
                            "", "stories_simulation", tmp_location_->path(), QuitLoopClosure(),
                            &ledger_, kDefaultGarbageCollectionPolicy);
  if (QuitOnError(QuitLoopClosure(), status, "GetLedger")) {
    return;
  }

  InitializeDefaultPages();
}

void StoriesBenchmark::InitializeDefaultPages() {
  TRACE_DURATION("benchmarks", "initialize_default_pages");
  TRACE_ASYNC_BEGIN("benchmark", "default_pages_initialization", 0);
  ledger_->GetPage(MakePageId(kMessageQueuePageId), message_queue_page_.NewRequest());
  ledger_->GetPage(MakePageId(kAgentRunnerPageId), agent_runner_page_.NewRequest());
  ledger_->GetRootPage(root_page_.NewRequest());

  // Regitster watchers
  AddWatcher(&message_queue_page_, "", &message_queue_watcher_);
  AddWatcher(&agent_runner_page_, "", &agent_runner_watcher_);
  AddWatcher(&root_page_, "Story/", &root_watcher_on_story_);
  AddWatcher(&root_page_, "Focus/", &root_watcher_on_focus_);

  auto waiter = MakeRefCounted<CompletionWaiter>();

  // Get entries from the agent runner page.
  ReadAllFromPage(&agent_runner_page_, convert::ToArray(""), waiter->NewCallback());

  // Wait for previous operations to finish and start creating stories.
  root_page_->Sync(waiter->NewCallback());
  agent_runner_page_->Sync(waiter->NewCallback());
  message_queue_page_->Sync(waiter->NewCallback());
  waiter->Finalize([this] {
    TRACE_ASYNC_END("benchmark", "default_pages_initialization", 0);
    RunSingle(0);
  });
}

void StoriesBenchmark::RunSingle(int i) {
  if (i == static_cast<int>(story_count_)) {
    ShutDown();
    return;
  }
  if (wait_for_cached_page_) {
    // Add a delay before each story creation to get the performance in Ledger's best working
    // conditions.
    zx_nanosleep(zx_deadline_after(kDelay.get()));
  }
  std::vector<uint8_t> story_name = GetStoryName(i);
  std::vector<uint8_t> story_data = generator_.MakeValue(kStoryValueSize);

  TRACE_ASYNC_BEGIN("benchmark", "story_lifetime", i);

  auto waiter = MakeRefCounted<CompletionWaiter>();
  ReadFromPage(&root_page_, story_name, waiter->NewCallback());

  PagePtr story_page;
  ledger_->GetPage(nullptr, story_page.NewRequest());

  ActiveStory story;
  story.connection1 = std::move(story_page);
  active_stories_.push_back(std::move(story));
  PagePtr& story_page_ref = active_stories_.back().connection1;

  story_page_ref->GetId([this, i, callback = waiter->NewCallback()](PageId story_id) mutable {
    EditStory(i, story_id, std::move(callback));
  });

  page_data_generator_.PutEntry(&root_page_, story_name, std::move(story_data),
                                PageDataGenerator::ReferenceStrategy::REFERENCE, Priority::EAGER,
                                CheckStatusOKCallback());

  // Yes, the content of the story is read 3 more times.
  for (int j = 0; j < 3; j++) {
    ReadFromPage(&root_page_, story_name, waiter->NewCallback());
  }
  root_page_->Sync(waiter->NewCallback());
  story_page_ref->Sync(waiter->NewCallback());

  waiter->Finalize([this, i] {
    TRACE_ASYNC_END("benchmark", "story_lifetime", i);

    // Measure memory before the cleanup.
    uint64_t memory;
    LEDGER_CHECK(memory_estimator_->GetLedgerMemoryUsage(&memory));
    TRACE_COUNTER("benchmark", "memory_stories", i, "memory", TA_UINT64(memory));

    MaybeCleanup(i, [this, i] { RunSingle(i + 1); });
  });
}

// Opens a second connection to the page and updates its contents.
void StoriesBenchmark::EditStory(int i, PageId story_id, fit::function<void()> callback) {
  PagePtr story_page;
  ledger_->GetPage(std::make_unique<PageId>(story_id), story_page.NewRequest());
  ActiveStory& story = active_stories_.back();
  story.story_id = story_id;
  story.connection2 = std::move(story_page);
  PagePtr* story_ptr = &story.connection2;

  // This intentionally invalidates the watcher from the previous story: Even if
  // multiple are active, a single one will be written to, and thus receive
  // watcher notifications.
  AddWatcher(story_ptr, "", &story_watcher1_);

  std::vector<uint8_t> link_key = GetLinkKey(i);
  auto waiter = MakeRefCounted<CompletionWaiter>();
  ReadFromPage(story_ptr, link_key, waiter->NewCallback());

  AddWatcher(story_ptr, "", &story_watcher2_);

  ReadAllFromPage(story_ptr, convert::ToArray("Module/"), waiter->NewCallback());

  page_data_generator_.PutEntry(
      story_ptr, std::move(link_key), generator_.MakeValue(kLinkValueSize),
      PageDataGenerator::ReferenceStrategy::REFERENCE, Priority::EAGER, CheckStatusOKCallback());

  std::vector<uint8_t> module_key = GetModuleKey(i);
  ReadFromPage(story_ptr, module_key, waiter->NewCallback());

  page_data_generator_.PutEntry(story_ptr, module_key, generator_.MakeValue(kModuleValueSize),
                                PageDataGenerator::ReferenceStrategy::REFERENCE, Priority::EAGER,
                                CheckStatusOKCallback());
  ReadFromPage(story_ptr, module_key, waiter->NewCallback());

  (*story_ptr)->Sync(waiter->NewCallback());
  waiter->Finalize(std::move(callback));
}

void StoriesBenchmark::MaybeCleanup(int i, fit::function<void()> callback) {
  LEDGER_DCHECK(active_stories_.size() <= active_story_count_);
  // After the |i|th story, |i + 1| stories have been created in total.
  size_t stories_created = i + 1;
  if (stories_created < active_story_count_) {
    // We don't have enough active pages, so don't clean up yet.
    callback();
    return;
  }
  // After having |active_story_count_| stories active, we can remove the least recently used one
  // from the active stories list.
  ClearLRUPage(stories_created - active_story_count_, std::move(callback));
}

void StoriesBenchmark::ClearLRUPage(int story_index, fit::function<void()> callback) {
  // Clear and close the LRU page, i.e. the first element of |active_stories_|.
  TRACE_ASYNC_BEGIN("benchmark", "story_cleanup", story_index);
  auto waiter = MakeRefCounted<CompletionWaiter>();

  ActiveStory& story = active_stories_.front();

  ledger_->GetPage(std::make_unique<PageId>(story.story_id),
                   story.connection_for_clear.NewRequest());
  story.connection_for_clear->Clear();
  story.connection_for_clear->Sync(waiter->NewCallback());

  root_page_->Delete(GetStoryName(story_index));
  root_page_->Sync(waiter->NewCallback());

  waiter->Finalize([this, story_index, callback = std::move(callback)] {
    TRACE_ASYNC_END("benchmark", "story_cleanup", story_index);

    // Close all remaining connections to the page.
    active_stories_.pop_front();
    callback();
  });
}

void StoriesBenchmark::ClearRemainingPages(size_t story_index, fit::function<void()> callback) {
  if (story_index == story_count_) {
    callback();
    return;
  }
  ClearLRUPage(story_index, [this, story_index, callback = std::move(callback)]() mutable {
    ClearRemainingPages(story_index + 1, std::move(callback));
  });
}

void StoriesBenchmark::ShutDown() {
  size_t i = story_count_ - active_story_count_ + 1;
  ClearRemainingPages(i, [this] {
    LEDGER_DCHECK(active_stories_.empty());

    // Shut down the Ledger process first as it relies on |tmp_location_| storage.
    KillLedgerProcess(&component_controller_);
    loop_->Quit();
  });
}

fit::closure StoriesBenchmark::QuitLoopClosure() {
  return [this] { loop_->Quit(); };
}

int Main(int argc, char** argv) {
  absl::ParseCommandLine(argc, argv);

  async::Loop loop(&kAsyncLoopConfigAttachToCurrentThread);
  auto component_context = sys::ComponentContext::Create();

  ssize_t story_count = absl::GetFlag(FLAGS_story_count);
  ssize_t active_story_count = absl::GetFlag(FLAGS_active_story_count);
  bool wait_for_cached_page = absl::GetFlag(FLAGS_wait_for_cached_page);

  if (story_count <= 0 || active_story_count < 0) {
    std::cerr << "Incorrect parameter values" << std::endl;
    return 1;
  }

  StoriesBenchmark app(&loop, std::move(component_context), story_count, active_story_count,
                       wait_for_cached_page);

  return RunWithTracing(&loop, [&app] { app.Run(); });
}

}  // namespace
}  // namespace ledger

int main(int argc, char** argv) { return ledger::Main(argc, argv); }
