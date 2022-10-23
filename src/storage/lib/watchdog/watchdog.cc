// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <lib/syslog/global.h>
#include <lib/watchdog/operations.h>
#include <lib/watchdog/watchdog.h>
#include <lib/zx/process.h>
#include <lib/zx/result.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/compiler.h>
#include <zircon/errors.h>
#include <zircon/time.h>

#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <inspector/inspector.h>

namespace fs_watchdog {

namespace {

// Current syslog implementation has a buffer limit per call. This forces us to
// split and log the messages.
void DumpLog(const char* log_tag, const char* str) {
  std::istringstream stream(str);
  std::string line;
  while (std::getline(stream, line)) {
    FX_LOGF(INFO, log_tag, "%s", line.c_str());
  }
}

class Watchdog : public WatchdogInterface {
 public:
  explicit Watchdog(const Options& options = {}) : options_(options) {}

  Watchdog(const Watchdog&) = delete;
  Watchdog(Watchdog&&) = delete;
  Watchdog& operator=(const Watchdog&) = delete;
  Watchdog& operator=(Watchdog&&) = delete;

  ~Watchdog() override { [[maybe_unused]] auto status = ShutDown(); }

  zx::result<> Start() final;
  zx::result<> ShutDown() final;
  zx::result<> Track(OperationTracker* tracker) final;
  zx::result<> Untrack(OperationTrackerId tracker_id) final;

 private:
  // Worker routine that scans the list of in-flight trackers. Returns only if
  // awakened by completion_ signal.
  void Run();

  // Thread which periodically watches in-flight operations.
  std::thread thread_;

  // Protects access to the state of the watchdog.
  std::mutex lock_;

  // Map that contains all in-flight healthy(non-timed-out) operations.
  // When watchdog is enabled, we do not want IO paths to get impacted.
  // map is not the ideal, as it allocates and frees entries, but is convenient.
  // We should have a pool of objects or the likes eventually.
  std::map<OperationTrackerId, OperationTracker*> healthy_operations_ __TA_GUARDED(lock_);

  // Map that contains all in-flight timed-out operations.
  std::map<OperationTrackerId, OperationTracker*> timed_out_operations_ __TA_GUARDED(lock_);

  // Set to true when watchdog thread is spun-up and is set to false when the
  // thread is torn-down.
  bool running_ __TA_GUARDED(lock_) = false;

  const Options options_;

  sync_completion_t completion_;
};

zx::result<> Watchdog::Track(OperationTracker* tracker) {
  std::lock_guard<std::mutex> lock(lock_);
  if (!options_.enabled) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  auto found = timed_out_operations_.find(tracker->GetId());
  if (found != timed_out_operations_.end()) {
    return zx::error(ZX_ERR_ALREADY_EXISTS);
  }

  auto ret = healthy_operations_.insert(
      std::pair<OperationTrackerId, OperationTracker*>(tracker->GetId(), tracker));
  if (!ret.second) {
    return zx::error(ZX_ERR_ALREADY_EXISTS);
  }
  return zx::ok();
}

zx::result<> Watchdog::Untrack(OperationTrackerId id) {
  OperationTracker* tracker;
  bool timed_out = false;
  {
    std::lock_guard<std::mutex> lock(lock_);
    auto found = healthy_operations_.find(id);
    if (found == healthy_operations_.end()) {
      found = timed_out_operations_.find(id);
      if (found == timed_out_operations_.end()) {
        return zx::error(ZX_ERR_NOT_FOUND);
      }
      tracker = found->second;
      timed_out_operations_.erase(found);
      timed_out = true;
    } else {
      tracker = found->second;
      healthy_operations_.erase(found);
    }
  }

  // If this was a timed-out operation and we have logged message before,
  // log another message saying this operation completed but took longer than
  // anticipated.
  if (!timed_out) {
    return zx::ok();
  }
  auto now = std::chrono::steady_clock::now();
  auto time_elapsed = now - tracker->StartTime();
  FX_LOGF(INFO, options_.log_tag.c_str(),
          "Timeout(%lluns) exceeded operation:%s id:%lu completed(%lluns).",
          tracker->Timeout().count(), tracker->Name().data(), tracker->GetId(),
          time_elapsed.count());
  return zx::ok();
}

void Watchdog::Run() {
  // TODO(fxbug.dev/58179)
  // Inspect debug printer only accepts a FILE stream for output, but we don't
  // want to hold the lock while actually flushing out to log. This buffer is
  // used as a temporary destination to queue lines and thread information so it
  // can be sent to the log after releasing the lock.
  std::unique_ptr<char[]> log_buffer = std::make_unique<char[]>(options_.log_buffer_size);
  // FILE stream on top of log_buffer that is used to get stack traces.
  std::unique_ptr<FILE, decltype(&fclose)> out_stream(
      fmemopen(log_buffer.get(), options_.log_buffer_size, "r+"), &fclose);

  while (true) {
    // Right now we periodically wakeup and scan all the trackers for timeout.
    // This is OK as long as few operations are in flight. The code needs to
    // sort and scan only entries that have timed out. Also, sleep can be for a
    // duration till next potential timeout.
    auto should_terminate =
        sync_completion_wait(&completion_, zx_duration_from_nsec(options_.sleep.count())) == ZX_OK;

    bool log = false;
    {
      std::lock_guard<std::mutex> lock(lock_);
      if (should_terminate) {
        running_ = false;
        ZX_ASSERT(healthy_operations_.empty());
        ZX_ASSERT(timed_out_operations_.empty());
        return;
      }
      auto now = std::chrono::steady_clock::now();
      std::map<OperationTrackerId, OperationTracker*>::iterator iter;
      rewind(out_stream.get());
      for (iter = healthy_operations_.begin(); iter != healthy_operations_.end();) {
        auto tracker = iter->second;
        std::chrono::nanoseconds time_elapsed = now - tracker->StartTime();

        // Avoid logging messages for this operation if we have already logged once.
        if (!tracker->TimedOut()) {
          ++iter;
          continue;
        }
        iter = healthy_operations_.erase(iter);
        timed_out_operations_.insert({tracker->GetId(), tracker});
        fprintf(out_stream.get(), "Operation:%s id:%lu exceeded timeout(%lluns < %lluns)\n",
                tracker->Name().data(), tracker->GetId(), tracker->Timeout().count(),
                time_elapsed.count());
        log = true;
        tracker->OnTimeOut(out_stream.get());
      }
    }
    if (log) {
      inspector_print_debug_info_for_all_threads(out_stream.get(), zx_process_self());
      fflush(out_stream.get());
      DumpLog(options_.log_tag.c_str(), log_buffer.get());
    }
  }
}

zx::result<> Watchdog::Start() {
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (!options_.enabled || running_) {
      return zx::error(ZX_ERR_BAD_STATE);
    }
    healthy_operations_.clear();
    timed_out_operations_.clear();
    sync_completion_reset(&completion_);
    thread_ = std::thread([this] { Run(); });
    running_ = true;
  }
  return zx::ok();
}

zx::result<> Watchdog::ShutDown() {
  if (!thread_.joinable()) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (!options_.enabled || !running_) {
      return zx::error(ZX_ERR_BAD_STATE);
    }
    sync_completion_signal(&completion_);
  }
  thread_.join();
  return zx::ok();
}

}  // namespace

std::unique_ptr<WatchdogInterface> CreateWatchdog(const Options& options) {
  auto watchdog = new Watchdog(options);
  std::unique_ptr<WatchdogInterface> ret(watchdog);
  return ret;
}

}  // namespace fs_watchdog
