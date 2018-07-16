// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/debugger_utils/jobs.h"

#include <inttypes.h>
#include <stdlib.h>

#include <list>

#include <lib/zx/job.h>
#include <lib/zx/process.h>
#include <lib/zx/thread.h>
#include <zircon/syscalls.h>

#include "garnet/lib/debugger_utils/util.h"

#include "lib/fxl/logging.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/strings/string_printf.h"

namespace debugger_utils {

struct WalkContext {
  JobTreeJobCallback* job_callback;
  JobTreeProcessCallback* process_callback;
  JobTreeThreadCallback* thread_callback;
};

// When reallocating koid buffer because we were too small add this much extra
// on top of what the kernel says is currently needed.
static const size_t kNumExtraKoids = 10;

class KoidTable {
 public:
  explicit KoidTable()
      : koids_(&initial_buf_[0]), size_(0), capacity_(kNumInitialKoids) {}

  ~KoidTable() {
    if (koids_ != &initial_buf_[0]) {
      delete[] koids_;
    }
  }

  size_t size() const { return size_; }
  size_t capacity() const { return capacity_; }
  zx_koid_t* data() const { return koids_; }
  bool empty() const { return size_ == 0; }

  size_t CapacityInBytes() const { return capacity() * sizeof(zx_koid_t); }

  zx_koid_t Get(size_t index) const {
    FXL_DCHECK(index < size_);
    return koids_[index];
  }

  // If this fails then we just continue to use the current capacity.
  void TryReserve(size_t new_capacity) {
    auto new_buf = malloc(new_capacity * sizeof(zx_koid_t));
    if (new_buf) {
      if (koids_ != &initial_buf_[0]) {
        free(koids_);
      }
      koids_ = reinterpret_cast<zx_koid_t*>(new_buf);
      capacity_ = new_capacity;
    }
  }

  void SetSize(size_t size) {
    FXL_DCHECK(size <= capacity_);
    size_ = size;
  }

 private:
  // Allocate space for this many koids within the object, it can live on the
  // stack. Only if we need more than this do we use the heap.
  // Note that for jobs, due to potentially deep nesting, this object should
  // always be allocated in the heap.
  static constexpr size_t kNumInitialKoids = 32;
  zx_koid_t initial_buf_[kNumInitialKoids];

  // A pointer to |initial_buf_| or a heap-allocated object.
  zx_koid_t* koids_;

  // The number of entries in |koids_|.
  size_t size_;

  // The amount of koids available in |koids_|.
  size_t capacity_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(KoidTable);
};

// A stack of job koid tables, to avoid recursion.
// Jobs aren't normally nested *that* deep, but we can't assume that of course.
class JobKoidTableStackEntry {
 public:
  explicit JobKoidTableStackEntry(zx::job job, zx_koid_t jid, int depth,
                                  std::unique_ptr<KoidTable> subjob_koids)
      : job_(std::move(job)),
        jid_(jid),
        depth_(depth),
        subjob_koids_(std::move(subjob_koids)),
        current_subjob_index_(0) {}

  zx_handle_t job() const { return job_.get(); }

  zx_koid_t jid() const { return jid_; }

  int depth() const { return depth_; }

  bool empty() const { return current_subjob_index_ == subjob_koids_->size(); }

  zx_koid_t PopNext() {
    FXL_DCHECK(!empty());
    auto koid = subjob_koids_->Get(current_subjob_index_);
    ++current_subjob_index_;
    return koid;
  }

 private:
  zx::job job_;
  zx_koid_t jid_;
  int depth_;
  std::unique_ptr<KoidTable> subjob_koids_;
  size_t current_subjob_index_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(JobKoidTableStackEntry);
};

using JobKoidTableStack = std::list<JobKoidTableStackEntry>;

static zx_status_t GetChild(zx_handle_t parent, zx_koid_t parent_koid,
                            zx_koid_t koid, zx_handle_t* task) {
  auto status = zx_object_get_child(parent, koid, ZX_RIGHT_SAME_RIGHTS, task);
  if (status != ZX_OK) {
    // The task could have terminated in the interim.
    if (status == ZX_ERR_NOT_FOUND) {
      FXL_VLOG(1) << fxl::StringPrintf(
          "zx_object_get_child(%" PRIu64 ", %" PRIu64 ", ...) failed: %s\n",
          parent_koid, koid, ZxErrorString(status).c_str());
    } else {
      FXL_LOG(ERROR) << fxl::StringPrintf(
          "zx_object_get_child(%" PRIu64 ", %" PRIu64 ", ...) failed: %s\n",
          parent_koid, koid, ZxErrorString(status).c_str());
    }
  }
  return status;
}

static zx_status_t FetchChildren(zx_handle_t parent, zx_koid_t parent_koid,
                                 int children_kind, const char* kind_name,
                                 KoidTable* koids) {
  size_t actual = 0;
  size_t avail = 0;
  zx_status_t status;

  // This is inherently racy, but we retry once with a bit of slop to try to
  // get a complete list.
  for (int pass = 0; pass < 2; ++pass) {
    if (actual < avail) {
      koids->TryReserve(avail + kNumExtraKoids);
    }
    status = zx_object_get_info(parent, children_kind, koids->data(),
                                koids->CapacityInBytes(), &actual, &avail);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << fxl::StringPrintf(
          "zx_object_get_info(%" PRIu64 ", %s, ...) failed: %s\n", parent_koid,
          kind_name, ZxErrorString(status).c_str());
      return status;
    }
    if (actual == avail) {
      break;
    }
  }

  // If we're still too small at least warn the user.
  if (actual < avail) {
    FXL_LOG(WARNING) << fxl::StringPrintf("zx_object_get_info(%" PRIu64
                                          ", %s, ...)"
                                          " truncated results, got %zu/%zu\n",
                                          parent_koid, kind_name, actual,
                                          avail);
  }

  koids->SetSize(actual);
  return ZX_OK;
}

static zx_status_t DoThreads(const WalkContext* ctx, zx_handle_t process,
                             zx_handle_t pid, int depth) {
  FXL_DCHECK(ctx->thread_callback);

  KoidTable koids;
  auto status = FetchChildren(process, pid, ZX_INFO_PROCESS_THREADS,
                              "ZX_INFO_PROCESS_THREADS", &koids);
  if (status != ZX_OK) {
    return status;
  }

  for (size_t i = 0; i < koids.size(); ++i) {
    zx_handle_t child;
    zx_koid_t tid = koids.Get(i);
    status = GetChild(process, pid, tid, &child);
    if (status != ZX_OK) {
      continue;
    }
    zx::thread thread(child);

    status = (*ctx->thread_callback)(thread, tid, pid, depth);
    if (status != ZX_OK) {
      return status;
    }

    // There's nothing special we need to do here if the callback took
    // ownership of the handle.
  }

  return ZX_OK;
}

static zx_status_t DoProcesses(const WalkContext* ctx, zx_handle_t job,
                               zx_handle_t jid, int depth) {
  KoidTable koids;
  auto status = FetchChildren(job, jid, ZX_INFO_JOB_PROCESSES,
                              "ZX_INFO_JOB_PROCESSES", &koids);
  if (status != ZX_OK) {
    return status;
  }

  for (size_t i = 0; i < koids.size(); ++i) {
    zx_handle_t child;
    zx_koid_t pid = koids.Get(i);
    status = GetChild(job, jid, pid, &child);
    if (status != ZX_OK) {
      continue;
    }
    zx::process process(child);
    if (ctx->process_callback) {
      status = (*ctx->process_callback)(process, pid, jid, depth);
      if (status != ZX_OK) {
        return status;
      }
    }
    // If the callback took ownership of the process handle we can still scan
    // the threads using |child|. The callback is required to not close the
    // process handle until WalkJobTree() returns.
    if (ctx->thread_callback) {
      status = DoThreads(ctx, child, pid, depth + 1);
      if (status != ZX_OK) {
        return status;
      }
    }
  }

  return ZX_OK;
}

static zx_status_t DoJob(JobKoidTableStack* stack, const WalkContext* ctx,
                         zx::job& job, bool job_is_top_level_job, zx_koid_t jid,
                         zx_koid_t parent_jid, int depth) {
  zx_status_t status;

  // Things are a bit tricky here as |job_callback| could take ownership of
  // the job, but we still need to call DoProcesses().
  auto job_h = job.get();
  if (ctx->job_callback) {
    status = (*ctx->job_callback)(job, jid, parent_jid, depth);
    if (status != ZX_OK) {
      return status;
    }
  }

  if (ctx->process_callback || ctx->thread_callback) {
    status = DoProcesses(ctx, job_h, jid, depth + 1);
    if (status != ZX_OK) {
      return status;
    }
  }

  // If the job callback took ownership of the handle, that's it, we
  // can't continue.
  if (!job.is_valid()) {
    return ZX_ERR_STOP;
  }

  auto subjob_koids = std::unique_ptr<KoidTable>(new KoidTable());
  status = FetchChildren(job_h, jid, ZX_INFO_JOB_CHILDREN,
                         "ZX_INFO_JOB_CHILDREN", subjob_koids.get());
  if (status != ZX_OK) {
    return status;
  }
  if (!subjob_koids->empty()) {
    if (job_is_top_level_job) {
      // Don't consume the |job| argument to WalkJobTree(), make a dupe.
      // We've already processed the job, so if the callback was going to
      // consume it, it already would have.
      zx::job dupe_job;
      status = job.duplicate(ZX_RIGHT_SAME_RIGHTS, &dupe_job);
      if (status != ZX_OK)
        return status;
      stack->emplace_front(std::move(dupe_job), jid, depth + 1,
                           std::move(subjob_koids));
    } else {
      stack->emplace_front(std::move(job), jid, depth + 1,
                           std::move(subjob_koids));
    }
  }

  return ZX_OK;
}

static zx_status_t WalkJobTreeInternal(JobKoidTableStack* stack,
                                       const WalkContext* ctx) {
  while (!stack->empty()) {
    auto& stack_entry = stack->front();
    if (stack_entry.empty()) {
      stack->pop_front();
    } else {
      auto parent_job = stack_entry.job();
      auto parent_jid = stack_entry.jid();
      auto depth = stack_entry.depth();
      auto jid = stack_entry.PopNext();
      zx_handle_t job_h;
      auto status = GetChild(parent_job, parent_jid, jid, &job_h);
      if (status != ZX_OK) {
        return status;
      }

      zx::job job(job_h);
      status = DoJob(stack, ctx, job, false, jid, parent_jid, depth);
      if (status != ZX_OK) {
        return status;
      }
    }
  }

  return ZX_OK;
}

zx_status_t WalkJobTree(zx::job& job, JobTreeJobCallback* job_callback,
                        JobTreeProcessCallback* process_callback,
                        JobTreeThreadCallback* thread_callback) {
  FXL_DCHECK(job != ZX_HANDLE_INVALID);
  zx_info_handle_basic_t info;
  auto status = zx_object_get_info(job.get(), ZX_INFO_HANDLE_BASIC, &info,
                                   sizeof(info), nullptr, nullptr);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << fxl::StringPrintf(
        "zx_object_get_info(search_job, ZX_INFO_HANDLE_BASIC, ...) failed: "
        "%s\n",
        ZxErrorString(status).c_str());
    return status;
  }
  auto jid = info.koid;
  auto parent_jid = info.related_koid;

  WalkContext ctx;
  ctx.job_callback = job_callback;
  ctx.process_callback = process_callback;
  ctx.thread_callback = thread_callback;

  JobKoidTableStack stack;
  status = DoJob(&stack, &ctx, job, true, jid, parent_jid, 0);
  if (status != ZX_OK) {
    return status;
  }

  return WalkJobTreeInternal(&stack, &ctx);
}

zx::process FindProcess(zx::job& job, zx_koid_t pid) {
  zx::process process;
  JobTreeProcessCallback find_process_callback =
      [&](zx::process& task, zx_koid_t koid, zx_koid_t parent_koid,
          int depth) -> zx_status_t {
    if (koid == pid) {
      process.reset(task.release());
      return ZX_ERR_STOP;
    }
    return ZX_OK;
  };
  // There's no real need to check the result here.
  WalkJobTree(job, nullptr, &find_process_callback, nullptr);
  return process;
}

zx::process FindProcess(zx_handle_t job_h, zx_koid_t pid) {
  zx::job job(job_h);
  auto result = FindProcess(job, pid);
  // Don't close |job_h| when we return.
  auto released_handle __UNUSED = job.release();
  return result;
}

// The default job is not ours to own so we need to make a copy.
// This is a simple wrapper to do that.
zx::job GetDefaultJob() {
  auto job = zx_job_default();
  if (job == ZX_HANDLE_INVALID) {
    FXL_VLOG(1) << "no default job";
    return zx::job();
  }
  zx_handle_t dupe;
  auto status = zx_handle_duplicate(job, ZX_RIGHT_SAME_RIGHTS, &dupe);
  if (status != ZX_OK) {
    FXL_LOG(ERROR) << "unable to create dupe of default job: "
                   << ZxErrorString(status);
    return zx::job();
  }

  return zx::job(dupe);
}

}  // namespace debugger_utils
