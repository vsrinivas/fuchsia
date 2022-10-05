// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_TASK_H_
#define SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_TASK_H_

#include <lib/fit/result.h>
#include <zircon/errors.h>
#include <zircon/syscalls/debug.h>

#include <functional>
#include <map>
#include <memory>
#include <utility>

#include <fbl/unique_fd.h>

#ifdef __Fuchsia__
#include <lib/zx/handle.h>
#endif

#include "types.h"

namespace zxdump {

// On Fuchsia, live task handles can be used via the lib/zx API.  On other
// systems, the API parts for live tasks are still available but they use a
// stub handle type that is always invalid.
#ifdef __Fuchsia__
using LiveTask = zx::handle;
#else
// The stub type is move-only and contextually convertible to bool just like
// the real one.  It supports only a few basic methods, which do nothing and
// always report an invalid handle.
struct LiveTask {
  LiveTask() = default;
  LiveTask(const LiveTask&) = delete;
  LiveTask(LiveTask&&) = default;
  LiveTask& operator=(const LiveTask&) = delete;
  LiveTask& operator=(LiveTask&&) = default;

  void reset() {}

  bool is_valid() const { return false; }

  explicit operator bool() const { return is_valid(); }

  zx_status_t get_info(uint32_t topic, void* buffer, size_t buffer_size, size_t* actual_count,
                       size_t* avail_count) const {
    return ZX_ERR_BAD_HANDLE;
  }

  zx_status_t get_property(uint32_t property, void* value, size_t size) const {
    return ZX_ERR_BAD_HANDLE;
  }

  zx_status_t get_child(uint64_t koid, zx_rights_t rights, LiveTask* result) const {
    return ZX_ERR_BAD_HANDLE;
  }
};
#endif

// Forward declarations for below.
class Task;
class Job;
class Process;
class Thread;

// This is the API for reading in dumps, both `ET_CORE` files and job archives.
//
// The zxdump::TaskHolder object is a container that holds the data from any
// number of dump files.  It provides access to the data as a Zircon job tree.
// The Job, Process, and Thread objects represent the jobs and processes found
// in dump files.  Each object provides calls analogous to the Zircon get_info,
// get_property, read_memory, and read_state calls, as well as get_child for
// nagivating a task tree.
//
// Dumps are inserted into the container by providing the file descriptor.  The
// type of file will be determined automatically from its contents.  Dump files
// can be ELF core dump (`ET_CORE`) files, or `ar` archive files.  An archive
// file can be a job archive or just a plain archive of other dump files.  Job
// archives can be mere "stub archives", or full hierarchical job archives, or
// flattened job archives.
//
// All the jobs and processes found in the dumps inserted then self-assemble
// into a job tree.  If the same task (same KOID) appears a second time either
// in two dump files or in two members of a job archive, insertion fails but
// may have added some of the tasks from the dump anyway.
//
// If every process and every job but one is a child of another job found in
// the dump so they all form a single job tree, then `root_job` returns the
// root of that tree.  If not, then `root_job` returns a fake "root job" with a
// KOID of 0 and no information or properties available except for the children
// and process lists.  These show all the jobs that don't have parent jobs that
// were dumped, i.e. the roots of job trees; and all the processes that aren't
// part of any dumped job at all.  Hence populating the container with a single
// ELF core dump will yield a fake root job whose sole child is that process.
//
// Methods that can fail use a result type with zxdump::Error.  When the
// `status_` field is ZX_ERR_IO, that means the failure was in a POSIXish
// filesystem access function and `errno` is set to indicate the exact error.
// Otherwise the error codes have mostly the same meaning they would have for
// the real Zircon calls, with some amendments:
//
//  * ZX_ERR_NOT_SUPPORTED just means the dump didn't include the requested
//    type of data.  It doesn't indicate whether the kernel didn't support it,
//    or the dump-writer intentionally chose not to dump it, or the dump was
//    just truncated, etc.
//
//  * Process::read_memory fails with ZX_ERR_NOT_FOUND if the dump indicated
//    the memory mapping existed but the dump did not include that memory.
//    ZX_ERR_OUT_OF_RANGE means the memory is absent because the dump was
//    truncated though this memory was intended to be included in the dump.
//    ZX_ERR_NO_MEMORY has the kernel's meaning that there was no memory
//    mapped at that address in the process.  ZX_ERR_NOT_SUPPORTED means that
//    the dump was inserted with the `read_memory=false` flag.
//
class TaskHolder {
 public:
  // Default-constructible, move-only.
  TaskHolder();
  TaskHolder(TaskHolder&&) = default;
  TaskHolder& operator=(TaskHolder&&) = default;
  ~TaskHolder();

  // Read the dump file from the file descriptor and insert its tasks.  If
  // `read_memory` is false, state will be trimmed after reading in all the
  // notes so less memory is used and the file descriptor is never kept open;
  // but read_memory calls will always fail with ZX_ERR_NOT_SUPPORTED.
  fit::result<Error> Insert(fbl::unique_fd fd, bool read_memory = true);

  // Insert a live task (job or process).  Live threads cannot be inserted
  // alone, only their containing process.
  fit::result<Error, std::reference_wrapper<Task>> Insert(LiveTask task);

  // Yields the current root job.  If all tasks in the eye of the TaskHolder
  // form a unified tree, this returns the actual root job in that tree.
  // Otherwise, this is the fake "root job" that reads as KOID 0 with no data
  // available except the Job::children and Job::processes lists holding each
  // orphaned task not claimed by any parent job.  It's always safe to hold
  // onto this reference for the life of the TaskHolder.  If more tasks are
  // added, this will start returning a different reference.  An old reference
  // to the fake root job will read as having no children and no processes if
  // all the tasks self-assembled into a single tree after more dumps were
  // inserted, and later start reporting new orphan tasks inserted after that.
  Job& root_job() const;

  // These can't fail, but return empty/zero if no corresponding data is in the
  // dump.  If multiple dumps supply system-wide information, only the first
  // dump's data will be used.  There is no checking that the system-wide data
  // in dumps is valid; malformed data may be treated like no data at all but
  // still may prevent well-formed data in other dumps from being used.
  uint32_t system_get_dcache_line_size() const;
  uint32_t system_get_num_cpus() const;
  uint64_t system_get_page_size() const;
  uint64_t system_get_physmem() const;
  std::string_view system_get_version_string() const;

 private:
  friend Task;
  friend Job;
  friend Process;
  friend Thread;
  class JobTree;

  std::unique_ptr<JobTree> tree_;
};

// As with zx::task, this is the superclass of Job, Process, and Thread.
// In fact, all the methods here correspond to the generic zx::object methods.
// But no objects that aren't tasks are found in dumps as such.
class Task {
 public:
  Task(Task&&) = default;
  Task& operator=(Task&&) = default;

  // Every task has a KOID.  This is just shorthand for extracting it from
  // ZX_INFO_HANDLE_BASIC.  The fake root job returns zero (ZX_KOID_INVALID).
  zx_koid_t koid() const;

  // This is a shorthand for extracting the type from ZX_INFO_HANDLE_BASIC.
  //  * If it returns ZX_OBJ_TYPE_JOB, `static_cast<Job&>(*this)` is safe.
  //  * If it returns ZX_OBJ_TYPE_PROCESS, `static_cast<Process&>(*this)` is.
  //  * If it returns ZX_OBJ_TYPE_THREAD, `static_cast<Thread&>(*this)` is.
  // The only task on which get_info<ZX_INFO_HANDLE_BASIC> can fail is the
  // fake root job; type() on it returns zero (ZX_OBJ_TYPE_NONE).
  zx_obj_type_t type() const;

  // This returns the timestamp of the dump, which may be zero.
  time_t date() const { return date_; }

  // This is provided for parity with zx::object::get_child, but just using
  // Process::threads, Job::children, or Job::processes is much more convenient
  // for iterating through the lists reported by get_info.
  fit::result<Error, std::reference_wrapper<Task>> get_child(zx_koid_t koid);

  // Find a task by KOID: this task or a descendent task.
  fit::result<Error, std::reference_wrapper<Task>> find(zx_koid_t koid);

  // This gets the full info block for this topic, whatever its size.  Note the
  // data is not necessarily aligned in memory, so it can't be safely accessed
  // with reinterpret_cast.
  fit::result<Error, ByteView> get_info(zx_object_info_topic_t topic, size_t record_size = 0);

  // Get statically-typed info for a topic chosen at a compile time.  Some
  // types return a single `zx_info_*_t` object.  Others return a span of const
  // type that points into storage permanently cached for the lifetime of the
  // containing TaskHolder.  See <lib/zxdump/types.h> for topic->type mappings.
  template <zx_object_info_topic_t Topic>
  fit::result<Error, InfoTraitsType<Topic>> get_info() {
    using Info = InfoTraitsType<Topic>;
    if constexpr (kIsSpan<Info>) {
      using Element = typename RemoveSpan<Info>::type;
      auto result = get_info_aligned(Topic, sizeof(Element), alignof(Element));
      if (result.is_error()) {
        return result.take_error();
      }
      return fit::ok(Info{
          reinterpret_cast<Element*>(result.value().data()),
          result.value().size() / sizeof(Element),
      });
    } else {
      auto result = get_info(Topic, sizeof(Info));
      if (result.is_error()) {
        return result.take_error();
      }
      ByteView bytes = result.value();
      Info data;
      if (bytes.size() < sizeof(data)) {
        return fit::error(Error{"truncated info note", ZX_ERR_NOT_SUPPORTED});
      }
      memcpy(&data, bytes.data(), sizeof(data));
      return fit::ok(data);
    }
  }

  // This gets the property, whatever its size.  Note the data is not
  // necessarily aligned in memory, so it can't be safely accessed with
  // reinterpret_cast.
  fit::result<Error, ByteView> get_property(uint32_t property);

  // Get a statically-typed property chosen at compile time.
  // See <lib/zxdump/types.h> for property->type mappings.
  template <uint32_t Property>
  fit::result<Error, PropertyTraitsType<Property>> get_property() {
    auto result = get_property(Property);
    if (result.is_error()) {
      return result.take_error();
    }
    ByteView bytes = result.value();
    PropertyTraitsType<Property> data;
    if (bytes.size() < sizeof(data)) {
      return fit::error(Error{"truncated property note", ZX_ERR_NOT_SUPPORTED});
    }
    memcpy(&data, bytes.data(), sizeof(data));
    return fit::ok(data);
  }

  // Turn a live task into a postmortem task.  The postmortem task holds only
  // the basic information (KOID, type) and whatever has been cached by past
  // get_info or get_property calls.
  LiveTask Reap() { return std::exchange(live_, {}); }

 protected:
  // The class is abstract.  Only the subclasses can be created and destroyed.
  Task() = delete;

  explicit Task(TaskHolder::JobTree& tree, LiveTask live = {})
      : tree_{tree}, live_(std::move(live)) {}

  ~Task();

  LiveTask& live() { return live_; }

  TaskHolder::JobTree& tree() { return tree_; }

 private:
  friend TaskHolder::JobTree;

  std::byte* GetBuffer(size_t size);
  void TakeBuffer(std::unique_ptr<std::byte[]> buffer);

  fit::result<Error, ByteView> get_info_aligned(zx_object_info_topic_t topic, size_t record_size,
                                                size_t align);
  fit::result<Error, ByteView> GetSuperrootInfo(zx_object_info_topic_t topic);

  // A plain reference would make the type not movable.
  std::reference_wrapper<TaskHolder::JobTree> tree_;
  std::map<zx_object_info_topic_t, ByteView> info_;
  std::map<uint32_t, ByteView> properties_;
  time_t date_ = 0;
  LiveTask live_;
};

// A Thread is a Task and also has register state.
class Thread : public Task {
 public:
  Thread(Thread&&) noexcept = default;
  Thread& operator=(Thread&&) noexcept = default;

  ~Thread();

  fit::result<Error, ByteView> read_state(zx_thread_state_topic_t topic);

 private:
  friend TaskHolder::JobTree;

  using Task::Task;

  std::map<zx_thread_state_topic_t, ByteView> state_;
};

// A Process is a Task and also has threads and memory.
class Process : public Task {
 public:
  using ThreadMap = std::map<zx_koid_t, Thread>;

  Process(Process&&) noexcept = default;
  Process& operator=(Process&&) noexcept = default;

  ~Process();

  // This is the same as what you'd get from get_info<ZX_INFO_PROCESS_THREADS>
  // and then get_child on each KOID, but pre-cached.  Note the returned map is
  // not const so the Thread references can be non-const, but the caller must
  // not modify the map itself.
  fit::result<Error, std::reference_wrapper<ThreadMap>> threads();

  // Find a task by KOID: this process or one of its threads.
  fit::result<Error, std::reference_wrapper<Task>> find(zx_koid_t koid);

 private:
  friend TaskHolder::JobTree;

  struct Segment {
    uint64_t offset, filesz, memsz;
  };

  using Task::Task;

  std::map<zx_koid_t, Thread> threads_;
  std::map<uint64_t, Segment> memory_;
};

// A Job is a Task and also has child jobs and processes.
class Job : public Task {
 public:
  using JobMap = std::map<zx_koid_t, Job>;
  using ProcessMap = std::map<zx_koid_t, Process>;

  Job(Job&&) noexcept = default;
  Job& operator=(Job&&) noexcept = default;

  ~Job();

  // This is the same as what you'd get from get_info<ZX_INFO_JOB_CHILDREN> and
  // then get_child on each KOID, but pre-cached.  Note the returned map is not
  // const so the Job references can be non-const, but the caller must not
  // modify the map itself.
  fit::result<Error, std::reference_wrapper<JobMap>> children();

  // This is the same as what you'd get from get_info<ZX_INFO_JOB_PROCESSES>
  // and then get_child on each KOID, but pre-cached.  Note the returned map is
  // not const so the Process references can be non-const, but the caller must
  // not modify the map itself.
  fit::result<Error, std::reference_wrapper<ProcessMap>> processes();

  // Find a task by KOID: this task or a descendent task.
  fit::result<Error, std::reference_wrapper<Task>> find(zx_koid_t koid);

 private:
  friend TaskHolder::JobTree;

  using Task::Task;

  fit::result<Error, ByteView> GetSuperrootInfo(zx_object_info_topic_t topic);

  std::map<zx_koid_t, Job> children_;
  std::map<zx_koid_t, Process> processes_;
};

// Get the live root job of the running system, e.g. for TaskHolder::Insert.
fit::result<Error, LiveTask> GetRootJob();

}  // namespace zxdump

#endif  // SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_TASK_H_
