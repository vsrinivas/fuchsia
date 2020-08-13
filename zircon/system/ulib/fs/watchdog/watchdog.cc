// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/sync/completion.h>
#include <lib/syslog/global.h>
#include <lib/zx/process.h>
#include <lib/zx/status.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/errors.h>
#include <zircon/time.h>

#include <cstdio>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>

#include <fs/locking.h>
#include <fs/watchdog/operations.h>
#include <fs/watchdog/watchdog.h>
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
  explicit Watchdog(const Options& options = {})
      : enabled_(options.enabled), sleep_(options.sleep), severity_(options.severity) {
    log_tag_ = options.log_tag;
    log_buffer_ = std::make_unique<char[]>(options.log_buffer_size);
    out_stream_ = fmemopen(log_buffer_.get(), options.log_buffer_size, "r+");
  }

  Watchdog(const Watchdog&) = delete;
  Watchdog(Watchdog&&) = delete;
  Watchdog& operator=(const Watchdog&) = delete;
  Watchdog& operator=(Watchdog&&) = delete;

  ~Watchdog() override { [[maybe_unused]] auto status = ShutDown(); }

  zx::status<> Start() final;
  zx::status<> ShutDown() final;
  zx::status<> Track(OperationTracker* tracker) final;
  zx::status<> Untrack(OperationTrackerId tracker_id) final;

 private:
  // Worker routine that scans the list of in-flight trackers. Returns only if
  // awakened by completion_ signal.
  void Run();

  // Thread which periodically watches in-flight operations.
  std::thread thread_;

  // Protects access to the state of the watchdog.
  std::mutex lock_;

  // True if watchdog is active and running.
  const bool enabled_;

  // The current implementation sleeps for fixed duration of time between two
  // scans. And when woken up, it scans *all* trackers to see if they have
  // timed out. This works well when there are few trackers registered but
  // becomes expensive when we have hundreds of operations to track. We can
  // optimize to sleep until next timeout and scan a list of operation sorted
  // by time to timeout.
  std::chrono::nanoseconds sleep_;
  [[maybe_unused]] fx_log_severity_t severity_;

  // Map that contains all in-flight healthy(non-timed-out) operations.
  // When watchdog is enabled, we do not want IO paths to get impacted.
  // map is not the ideal, as it allocates and frees entries, but is convenient.
  // We should have a pool of objects or the likes eventually.
  std::map<OperationTrackerId, OperationTracker*> healthy_operations_ FS_TA_GUARDED(lock_);

  // Map that contains all in-flight timed-out operations.
  std::map<OperationTrackerId, OperationTracker*> timed_out_operations_ FS_TA_GUARDED(lock_);

  // Set to true when watchdog thread is spun-up and is set to false when the
  // thread is torn-down.
  bool running_ FS_TA_GUARDED(lock_) = false;

  // User's tag for the log messages.
  std::string log_tag_;

  // Staging buffer log messages. Writing to log can
  // be slow especially when log is over serial device or permanent subsystem.
  // To keep the lock contention minimum, logs are written to this buffer under
  // lock and then the contents of this buffer are sent to logging subsystem
  // outside of this lock.
  // TODO(58179)
  std::unique_ptr<char[]> log_buffer_;

  // FILE stream on top of log_buffer that is used to get stack traces.
  FILE* out_stream_ = nullptr;

  sync_completion_t completion_;
};

zx::status<> Watchdog::Track(OperationTracker* tracker) {
  std::lock_guard<std::mutex> lock(lock_);
  if (!enabled_) {
    return zx::error(ZX_ERR_BAD_STATE);
  }

  auto ret = healthy_operations_.insert(
      std::pair<OperationTrackerId, OperationTracker*>(tracker->GetId(), tracker));
  if (ret.second == false) {
    return zx::error(ZX_ERR_ALREADY_EXISTS);
  }
  return zx::ok();
}

zx::status<> Watchdog::Untrack(OperationTrackerId id) {
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
  FX_LOGF(INFO, log_tag_.c_str(), "Timeout(%lluns) exceeded operation:%s id:%lu completed(%lluns).",
          tracker->Timeout().count(), tracker->Name().data(), tracker->GetId(),
          time_elapsed.count());
  return zx::ok();
}

void Watchdog::Run() {
  while (true) {
    // Right now we periodically wakeup and scan all the trackers for timeout.
    // This is OK as long as few operations are in flight. The code needs to
    // sort and scan only entries that have timed out. Also, sleep can be for a
    // duration till next potential timeout.
    auto should_terminate =
        sync_completion_wait(&completion_, zx_duration_from_nsec(sleep_.count())) == ZX_OK;

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
      rewind(out_stream_);
      for (iter = healthy_operations_.begin(); iter != healthy_operations_.end();) {
        auto tracker = iter->second;
        std::chrono::nanoseconds time_elapsed = now - tracker->StartTime();

        // Avoid logging messages for this operation if we have already logged once.
        if (!tracker->TimedOut()) {
          ++iter;
          continue;
        }
        healthy_operations_.erase(iter++);
        timed_out_operations_.insert({tracker->GetId(), tracker});
        fprintf(out_stream_, "Operation:%s id:%lu exceeded timeout(%lluns < %lluns)",
                tracker->Name().data(), tracker->GetId(), tracker->Timeout().count(),
                time_elapsed.count());
        log = true;
        tracker->OnTimeOut(out_stream_);
      }
    }
    if (log) {
      inspector_print_debug_info_for_all_threads(out_stream_, zx_process_self());
      fflush(out_stream_);
      DumpLog(log_tag_.c_str(), log_buffer_.get());
    }
  }
}

zx::status<> Watchdog::Start() {
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (!enabled_ || running_) {
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

zx::status<> Watchdog::ShutDown() {
  if (!thread_.joinable()) {
    return zx::error(ZX_ERR_BAD_STATE);
  }
  {
    std::lock_guard<std::mutex> lock(lock_);
    if (!enabled_ || !running_) {
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
