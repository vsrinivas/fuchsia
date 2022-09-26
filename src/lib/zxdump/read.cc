// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/defer.h>
#include <lib/stdcompat/string_view.h>
#include <lib/zxdump/task.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <zircon/assert.h>
#include <zircon/syscalls/object.h>

#include <algorithm>
#include <charconv>
#include <forward_list>
#include <variant>

#include <rapidjson/document.h>

#include "core.h"
#include "dump-file.h"
#include "job-archive.h"
#include "rights.h"

#ifdef __Fuchsia__
#include <lib/zx/job.h>
#include <lib/zx/process.h>
#endif

namespace zxdump {

using namespace internal;

namespace {

using namespace std::literals;

// The result of parsing an archive member header.  The name view may point
// into the original header buffer, so this must live no longer than that.
struct MemberHeader {
  std::string_view name;
  time_t date;
  size_t size;
};

std::string_view TrimSpaces(std::string_view string) {
  auto pos = string.find_last_not_of(' ');
  if (pos == std::string_view::npos) {
    return {};
  }
  return string.substr(0, pos + 1);
}

template <typename T>
bool ParseHeaderInteger(std::string_view field, T& value) {
  field = TrimSpaces(field);
  if (field.empty()) {
    // Some special members can have wholly blank integer fields and that's OK.
    value = 0;
    return true;
  }
  const char* first = field.data();
  const char* last = first + field.size();
  auto result = std::from_chars(first, last, value);
  return result.ptr == last && result.ec != std::errc::result_out_of_range;
}

// Parse the basic archive header.  The name may need additional decoding.
fitx::result<Error, MemberHeader> ParseArchiveHeader(ByteView header) {
  if (header.size() < sizeof(ar_hdr)) {
    return fitx::error(Error{"truncated archive", ZX_ERR_OUT_OF_RANGE});
  }
  static_assert(alignof(ar_hdr) == 1);
  auto ar = reinterpret_cast<const ar_hdr*>(header.data());
  if (!ar->valid()) {
    return CorruptedDump();
  }
  MemberHeader member{TrimSpaces({ar->ar_name, sizeof(ar->ar_name)}), 0, 0};
  if (!ParseHeaderInteger({ar->ar_date, sizeof(ar->ar_date)}, member.date) ||
      !ParseHeaderInteger({ar->ar_size, sizeof(ar->ar_size)}, member.size)) {
    return CorruptedDump();
  }
  return fitx::ok(member);
}

// Update member.name if it's an encoded reference to the long name table.
bool HandleLongName(std::string_view name_table, MemberHeader& member) {
  if (member.name.substr(0, ar_hdr::kLongNamePrefix.size()) == ar_hdr::kLongNamePrefix) {
    size_t name_offset = std::string_view::npos;
    if (!ParseHeaderInteger(member.name.substr(ar_hdr::kLongNamePrefix.size()), name_offset)) {
      return false;
    }
    member.name = name_table.substr(name_offset);
    size_t end = member.name.find(ar_hdr::kNameTableTerminator);
    if (end == 0 || end == std::string_view::npos) {
      return false;
    }
    member.name = member.name.substr(0, end);
  }
  return true;
}

// If name starts with match, then parse it as a note and store it in the map.
// The successful return value is false if the name didn't match or true if it
// was a valid note that wasn't already in the map.
template <typename Key>
fitx::result<Error, std::optional<Key>> JobNoteName(std::string_view match, std::string_view name) {
  if (name.substr(0, match.size()) == match) {
    name.remove_prefix(match.size());
    if (name.empty()) {
      return CorruptedDump();
    }
    Key key = 0;
    if (ParseHeaderInteger(name, key)) {
      return fitx::ok(key);
    }
  }
  return fitx::ok(std::nullopt);
}

// Add a note to an info_ or properties_ map.  Duplicates are not allowed.
template <typename Key>
fitx::result<Error> AddNote(std::map<Key, ByteView>& map, Key key, ByteView data) {
  auto [it, unique] = map.insert({key, data});
  if (!unique) {
    return fitx::error(Error{
        "duplicate note name in dump",
        ZX_ERR_IO_DATA_INTEGRITY,
    });
  }
  return fitx::ok();
}

// rapidjson's built-in features require NUL-terminated strings.
// Modeled on rapidjson::StringBuffer from <rapidjson/stringbuffer.h>.
class StringViewStream {
 public:
  using Ch = char;

  explicit StringViewStream(std::string_view data) : data_(data) {}

  Ch Peek() const { return data_.empty() ? '\0' : data_.front(); }

  Ch Take() {
    Ch c = data_.front();
    data_.remove_prefix(1);
    return c;
  }

  size_t Tell() const { return data_.size(); }

  Ch* PutBegin() {
    RAPIDJSON_ASSERT(false);
    return 0;
  }
  void Put(Ch) { RAPIDJSON_ASSERT(false); }
  void Flush() { RAPIDJSON_ASSERT(false); }
  size_t PutEnd(Ch*) {
    RAPIDJSON_ASSERT(false);
    return 0;
  }

 private:
  std::string_view data_;
};

constexpr Error kTaskNotFound{"task KOID not found", ZX_ERR_NOT_FOUND};

#ifdef __Fuchsia__
using LiveJob = zx::job;
using LiveProcess = zx::process;
#else
using LiveJob = LiveTask;
using LiveProcess = LiveTask;
#endif

using InsertChild = std::variant<std::monostate, Job, Process, Thread>;

}  // namespace

// This is the real guts of the zxdump::TaskHolder class.
class TaskHolder::JobTree {
 public:
  Job& root_job() const { return root_job_; }

  // Insert any number of dumps by reading a core file or an archive.
  fitx::result<Error> Insert(fbl::unique_fd fd, bool read_memory) {
    if (auto result = DumpFile::Open(std::move(fd)); result.is_error()) {
      return result.take_error();
    } else {
      dumps_.push_front(std::move(result).value());
    }
    auto& file = *dumps_.front();
    auto result = Read(file, read_memory, {0, file.size()});
    if (!read_memory) {
      file.shrink_to_fit();
    }
    if (file.size() == 0) {
      dumps_.pop_front();
    }
    Reroot();
    return result;
  }

  // Insert a live task.
  auto Insert(LiveTask live, InsertChild* parent)
      -> fitx::result<Error, std::reference_wrapper<Task>> {
    zx_info_handle_basic_t info;
    if (zx_status_t status =
            live.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
        status != ZX_OK) {
      return fitx::error(Error{"invalid live task", status});
    }

    // Place the basic info into a new Task object now that it's known valid.
    // Everything relies on the basic info always being available in the map.
    auto ingest = [&](auto attach, auto task)  //
        -> fitx::result<Error, std::reference_wrapper<Task>> {
      task.date_ = time(nullptr);  // Time of first data sample from this task.
      auto buffer = GetBuffer(sizeof(info));
      memcpy(buffer, &info, sizeof(info));
      task.info_.emplace(ZX_INFO_HANDLE_BASIC, ByteView{buffer, sizeof(info)});
      if (parent) {
        *parent = std::move(task);
        return fitx::ok(std::ref(std::get<decltype(task)>(*parent)));
      }
      return (this->*attach)(std::move(task));
    };

    switch (info.type) {
      case ZX_OBJ_TYPE_JOB:
        return ingest(&JobTree::AttachJob, Job{*this, std::move(live)});
      case ZX_OBJ_TYPE_PROCESS:
        return ingest(&JobTree::AttachProcess, Process{*this, std::move(live)});
      case ZX_OBJ_TYPE_THREAD:
        if (parent) {
          return ingest(static_cast<fitx::error<Error> (JobTree::*)(Thread)>(nullptr),
                        Thread{*this, std::move(live)});
        }
        [[fallthrough]];
      default:
        return fitx::error(Error{"not a valid job or process handle", ZX_ERR_BAD_HANDLE});
    }
  }

  void AssertIsSuperroot(Task& task) { ZX_DEBUG_ASSERT(&task == &superroot_); }

  // Unlike generic get_info, the view is always fully aligned for casting.
  fitx::result<Error, ByteView> GetSuperrootInfo(zx_object_info_topic_t topic) {
    switch (topic) {
      case ZX_INFO_JOB_CHILDREN:
        if (!superroot_info_children_) {
          // No value cached.
          zx_koid_t* p = new zx_koid_t[superroot_.children_.size()];
          superroot_info_children_.reset(p);
          for (const auto& [koid, job] : superroot_.children_) {
            *p++ = koid;
          }
        }
        return fitx::ok(ByteView{
            reinterpret_cast<const std::byte*>(superroot_info_children_.get()),
            superroot_.children()->get().size(),
        });

      case ZX_INFO_JOB_PROCESSES:
        if (!superroot_info_processes_) {
          // No value cached.
          zx_koid_t* p = new zx_koid_t[superroot_.processes_.size()];
          superroot_info_processes_.reset(p);
          for (const auto& [koid, job] : superroot_.processes_) {
            *p++ = koid;
          }
        }
        return fitx::ok(
            ByteView{reinterpret_cast<const std::byte*>(superroot_info_processes_.get()),
                     superroot_.processes()->get().size()});

      default:
        return fitx::error(Error{"fake root job info", ZX_ERR_NOT_SUPPORTED});
    }
  }

  // Allocate a buffer saved for the life of this holder.
  std::byte* GetBuffer(size_t size) {
    std::byte* buffer = new std::byte[size];
    buffers_.emplace_front(buffer);
    return buffer;
  }

  void TakeBuffer(std::unique_ptr<std::byte[]> owned_buffer) {
    buffers_.push_front(std::move(owned_buffer));
  }

  template <typename T>
  T GetSystemData(const char* key) const;

 private:
  // This is the actual reader, implemented below.
  fitx::result<Error> Read(DumpFile& file, bool read_memory, FileRange where, time_t date = 0);
  fitx::result<Error> ReadElf(DumpFile& file, FileRange where, time_t date, ByteView header,
                              bool read_memory);
  fitx::result<Error> ReadArchive(DumpFile& file, FileRange archive, ByteView header,
                                  bool read_memory);

  fitx::result<Error> ReadSystemNote(ByteView data);
  const rapidjson::Value* GetSystemJsonData(const char* key) const;

  // Snap the root job pointer to the sole job or back to the superroot.
  // Also clear the cached get_info lists so they'll be regenerated on demand.
  void Reroot() {
    if (superroot_.processes_.empty() && superroot_.children_.size() == 1) {
      auto& [koid, job] = *superroot_.children_.begin();
      root_job_ = std::ref(job);
    } else {
      root_job_ = std::ref(superroot_);
    }
    superroot_info_children_.reset();
    superroot_info_processes_.reset();
  }

  fitx::result<Error, std::reference_wrapper<Job>> AttachJob(Job&& job) {
    // See if any of the orphan jobs are this job's children.
    // If a child job is found in the superroot, claim it.
    if (!superroot_.children_.empty()) {
      auto result = job.get_info<ZX_INFO_JOB_CHILDREN>();
      if (result.is_ok()) {
        for (zx_koid_t koid : result.value()) {
          auto it = superroot_.children_.find(koid);
          if (it != superroot_.children_.end()) {
            superroot_info_children_.reset();  // Clear stale cache.
            auto [job_it, unique] = job.children_.insert(std::move(*it));
            superroot_.children_.erase(it);
            if (!unique) {
              return fitx::error(Error{
                  "duplicate job KOID",
                  ZX_ERR_IO_DATA_INTEGRITY,
              });
            }
          }
        }
      }
    }

    // See if any of the orphaned processes belong to this job.
    // If a process is found in the superroot, claim it.
    if (!superroot_.processes_.empty()) {
      auto result = job.get_info<ZX_INFO_JOB_PROCESSES>();
      if (result.is_ok()) {
        for (zx_koid_t koid : result.value()) {
          auto it = superroot_.processes_.find(koid);
          if (it != superroot_.processes_.end()) {
            superroot_info_processes_.reset();  // Clear stale cache.
            auto [job_it, unique] = job.processes_.insert(std::move(*it));
            superroot_.processes_.erase(it);
            if (!unique) {
              return fitx::error(Error{
                  "duplicate process KOID",
                  ZX_ERR_IO_DATA_INTEGRITY,
              });
            }
          }
        }
      }
    }

    // Now that it has wrangled its children, find this job's own parent.
    zx_koid_t koid = job.koid();
    if (auto it = missing_.find(koid); it != missing_.end()) {
      // There is a parent looking for this lost child!
      auto& [parent_koid, parent] = *it;
      auto [j, unique] = parent.children_.try_emplace(koid, std::move(job));
      ZX_DEBUG_ASSERT(unique);
      missing_.erase(it);
      return fitx::ok(std::ref(j->second));
    } else {
      // The superroot fosters the orphan until its parent appears (if ever).
      auto [j, unique] = superroot_.children_.try_emplace(koid, std::move(job));
      if (!unique) {
        return fitx::error(Error{
            "duplicate job KOID",
            ZX_ERR_IO_DATA_INTEGRITY,
        });
      }
      return fitx::ok(std::ref(j->second));
    }
  }

  fitx::result<Error, std::reference_wrapper<Process>> AttachProcess(Process&& process) {
    zx_koid_t koid = process.koid();
    if (auto it = missing_.find(koid); it != missing_.end()) {
      // There is a job looking for this lost process!
      auto& [job_koid, job] = *it;
      auto [p, unique] = job.processes_.try_emplace(koid, std::move(process));
      ZX_DEBUG_ASSERT(unique);
      missing_.erase(it);
      return fitx::ok(std::ref(p->second));
    }

    // The superroot holds the process until a job claims it (if ever).
    auto [it, unique] = superroot_.processes_.try_emplace(koid, std::move(process));
    if (!unique) {
      return fitx::error(Error{
          "duplicate process KOID",
          ZX_ERR_IO_DATA_INTEGRITY,
      });
    }

    return fitx::ok(std::ref(it->second));
  }

  std::forward_list<std::unique_ptr<DumpFile>> dumps_;
  std::forward_list<std::unique_ptr<std::byte[]>> buffers_;

  rapidjson::Document system_;

  // The superroot holds all the orphaned jobs and processes that haven't been
  // claimed by a parent job.
  Job superroot_{*this};

  // This records any dangling child or process KOIDs required by jobs already
  // in the holder.  When a matching task is attached, it goes to that job
  // instead of the superroot.
  std::map<zx_koid_t, Job&> missing_;

  // These are the buffers for the synthetic ZX_INFO_JOB_CHILDREN and
  // ZX_INFO_JOB_PROCESSES results returned by get_info calls on the superroot.
  // They are regenerated on demand, and cleared when new tasks are inserted.
  std::unique_ptr<zx_koid_t[]> superroot_info_children_, superroot_info_processes_;

  // The root job is either the superroot or its only child.
  std::reference_wrapper<Job> root_job_{superroot_};
};

// JobTree is an incomplete type outside this translation unit.  Some methods
// on TaskHolder et al need to access tree_, so they are defined here.

TaskHolder::TaskHolder() { tree_ = std::make_unique<JobTree>(); }

TaskHolder::~TaskHolder() = default;

Job& TaskHolder::root_job() const { return tree_->root_job(); }

fitx::result<Error> TaskHolder::Insert(fbl::unique_fd fd, bool read_memory) {
  return tree_->Insert(std::move(fd), read_memory);
}

fitx::result<Error, std::reference_wrapper<Task>> TaskHolder::Insert(LiveTask task) {
  return tree_->Insert(std::move(task), nullptr);
}

Task::~Task() = default;

Job::~Job() = default;

Process::~Process() = default;

Thread::~Thread() = default;

fitx::result<Error, std::reference_wrapper<zxdump::Job::JobMap>> Job::children() {
  if (children_.empty() && live()) {
    // The first time called on a live task (or on repeated calls iff the first
    // time there were no children), populate the whole list.
    auto result = get_info<ZX_INFO_JOB_CHILDREN>();
    if (result.is_error()) {
      return result.take_error();
    }
    LiveJob job{std::move(live())};
    auto restore = fit::defer([&]() { live() = std::move(job); });
    for (zx_koid_t koid : result.value()) {
      if (koid == ZX_KOID_INVALID) {
        continue;
      }
      LiveTask live_child;
      zx_status_t status = job.get_child(koid, kChildRights, &live_child);
      switch (status) {
        case ZX_OK:
          break;

        case ZX_ERR_NOT_FOUND:
          // It's not an error if the child has simply died already so the
          // KOID is no longer valid.
          continue;

        default:
          return fitx::error(Error{"zx_object_get_child", status});
      }

      InsertChild child;
      auto result = tree().Insert(std::move(live_child), &child);
      if (result.is_error()) {
        return result.take_error();
      }
      Job& child_job = std::get<Job>(child);
      ZX_ASSERT(child_job.koid() == koid);
      [[maybe_unused]] auto [it, unique] = children_.emplace(koid, std::move(child_job));
      ZX_DEBUG_ASSERT(unique);
    }
  }
  return fitx::ok(std::ref(children_));
}

fitx::result<Error, std::reference_wrapper<zxdump::Job::ProcessMap>> Job::processes() {
  if (processes_.empty() && live()) {
    // The first time called on a live task (or on repeated calls iff the first
    // time there were no processes), populate the whole list.
    auto result = get_info<ZX_INFO_JOB_PROCESSES>();
    if (result.is_error()) {
      return result.take_error();
    }
    LiveJob job{std::move(live())};
    auto restore = fit::defer([&]() { live() = std::move(job); });
    for (zx_koid_t koid : result.value()) {
      LiveTask live_process;
      zx_status_t status = job.get_child(koid, kChildRights, &live_process);
      switch (status) {
        case ZX_OK:
          break;

        case ZX_ERR_NOT_FOUND:
          // It's not an error if the process has simply died already so the
          // KOID is no longer valid.
          continue;

        default:
          return fitx::error(Error{"zx_object_get_child", status});
      }

      InsertChild child;
      auto result = tree().Insert(std::move(live_process), &child);
      if (result.is_error()) {
        return result.take_error();
      }
      Process& process = std::get<Process>(child);
      ZX_ASSERT(process.koid() == koid);
      [[maybe_unused]] auto [it, unique] = processes_.emplace(koid, std::move(process));
      ZX_DEBUG_ASSERT(unique);
    }
  }
  return fitx::ok(std::ref(processes_));
}

fitx::result<Error, std::reference_wrapper<zxdump::Process::ThreadMap>> Process::threads() {
  if (threads_.empty() && live()) {
    // The first time called on a live task (or on repeated calls iff the first
    // time there were no processes), populate the whole list.
    auto result = get_info<ZX_INFO_PROCESS_THREADS>();
    if (result.is_error()) {
      return result.take_error();
    }
    LiveProcess process{std::move(live())};
    auto restore = fit::defer([&]() { live() = std::move(process); });
    for (zx_koid_t koid : result.value()) {
      LiveTask live_thread;
      zx_status_t status = process.get_child(koid, kChildRights, &live_thread);
      switch (status) {
        case ZX_OK:
          break;

        case ZX_ERR_NOT_FOUND:
          // It's not an error if the thread has simply died already so the
          // KOID is no longer valid.
          continue;

        default:
          return fitx::error(Error{"zx_object_get_child", status});
      }

      InsertChild child;
      auto result = tree().Insert(std::move(live_thread), &child);
      if (result.is_error()) {
        return result.take_error();
      }

      Thread& thread = std::get<Thread>(child);
      ZX_ASSERT(thread.koid() == koid);
      [[maybe_unused]] auto [it, unique] = threads_.emplace(koid, std::move(thread));
      ZX_DEBUG_ASSERT(unique);
    }
  }

  return fitx::ok(std::ref(threads_));
}

fitx::result<Error, std::reference_wrapper<Task>> Task::find(zx_koid_t match) {
  if (koid() == match) {
    return fitx::ok(std::ref(*this));
  }
  switch (this->type()) {
    case ZX_OBJ_TYPE_JOB:
      return static_cast<Job*>(this)->find(match);
    case ZX_OBJ_TYPE_PROCESS:
      return static_cast<Process*>(this)->find(match);
  }
  return fitx::error{kTaskNotFound};
}

fitx::result<Error, std::reference_wrapper<Task>> Job::find(zx_koid_t match) {
  if (koid() == match) {
    return fitx::ok(std::ref(*this));
  }

  // First check our immediate child tasks.
  if (auto it = children_.find(match); it != children_.end()) {
    return fitx::ok(std::ref(it->second));
  }
  if (auto it = processes_.find(match); it != processes_.end()) {
    return fitx::ok(std::ref(it->second));
  }

  if (live()) {
    // Those maps aren't populated eagerly for live tasks.
    // Instead, just query the kernel for this one KOID first.
    LiveTask live_child;

    // zx::handle doesn't permit get_child, so momentarily move the live()
    // handle to a zx::job.  On non-Fuchsia, it's all still no-ops.
    LiveJob job{std::move(live())};
    zx_status_t status = job.get_child(match, kChildRights, &live_child);
    live() = std::move(job);

    if (status == ZX_OK) {
      // This is a child of ours, just not inserted yet.
      InsertChild child;
      auto result = tree().Insert(std::move(live_child), &child);
      if (result.is_error()) {
        return result.take_error();
      }

      if (auto job = std::get_if<Job>(&child)) {
        ZX_ASSERT(job->koid() == match);
        auto [it, unique] = children_.emplace(match, std::move(*job));
        ZX_DEBUG_ASSERT(unique);
        return fitx::ok(std::ref(it->second));
      }

      auto& process = std::get<Process>(child);
      ZX_ASSERT(process.koid() == match);
      auto [it, unique] = processes_.emplace(match, std::move(process));
      ZX_DEBUG_ASSERT(unique);
      return fitx::ok(std::ref(it->second));
    }
  }

  // For a live job, children() actively fills the children_ list.
  if (auto result = children(); result.is_error()) {
    return result.take_error();
  }

  // Recurse on the child jobs.
  for (auto& [koid, job] : children_) {
    auto result = job.find(match);
    if (result.is_ok()) {
      return result;
    }
  }

  // For a live job, processes() actively fills the processes_ list.
  if (auto result = processes(); result.is_error()) {
    return result.take_error();
  }

  // Recurse on the child processes.
  for (auto& [koid, process] : processes_) {
    auto result = process.find(match);
    if (result.is_ok()) {
      return result;
    }
  }

  return fitx::error{kTaskNotFound};
}

fitx::result<Error, std::reference_wrapper<Task>> Process::find(zx_koid_t match) {
  if (koid() == match) {
    return fitx::ok(std::ref(*this));
  }
  if (auto it = threads_.find(match); it != threads_.end()) {
    return fitx::ok(std::ref(it->second));
  }
  return fitx::error{kTaskNotFound};
}

std::byte* Task::GetBuffer(size_t size) { return tree().GetBuffer(size); }

void Task::TakeBuffer(std::unique_ptr<std::byte[]> buffer) { tree().TakeBuffer(std::move(buffer)); }

fitx::result<Error, ByteView> Task::GetSuperrootInfo(zx_object_info_topic_t topic) {
  tree_.get().AssertIsSuperroot(*this);
  return tree_.get().GetSuperrootInfo(topic);
}

fitx::result<Error, ByteView> Task::get_info_aligned(  //
    zx_object_info_topic_t topic, size_t record_size, size_t align) {
  ByteView bytes;
  if (auto result = get_info(topic, record_size); result.is_error()) {
    return result.take_error();
  } else {
    bytes = result.value();
  }

  void* ptr = const_cast<void*>(static_cast<const void*>(bytes.data()));
  size_t space = bytes.size();
  if (std::align(align, space, ptr, space)) {
    // It's already aligned.
    return fitx::ok(bytes);
  }

  // Allocate a buffer with alignment slop and make the holder hold onto it.
  space = bytes.size() + align - 1;
  ptr = tree_.get().GetBuffer(space);

  // Copy the data into the buffer with the right alignment.
  void* aligned_ptr = std::align(align, bytes.size(), ptr, space);
  memcpy(aligned_ptr, bytes.data(), bytes.size());

  // Return the aligned data in the buffer now held in the holder and replace
  // the cached data with the aligned copy for the next lookup to find.
  ByteView copy{static_cast<std::byte*>(aligned_ptr), bytes.size()};
  info_[topic] = copy;
  return fitx::ok(copy);
}

fitx::result<Error> TaskHolder::JobTree::Read(DumpFile& real_file, bool read_memory,
                                              FileRange where, time_t date) {
  // If the file is compressed, this will iterate with the decompressed file.
  for (DumpFile* file = &real_file; where.size >= kHeaderProbeSize;
       // Read the whole uncompressed file as a stream.  Its size is unknown.
       where = FileRange::Unbounded()) {
    ByteView header;
    if (auto result = file->ReadEphemeral(where / kHeaderProbeSize); result.is_error()) {
      return result.take_error();
    } else {
      header = result.value();
    }

    if (uint32_t word; memcpy(&word, header.data(), sizeof(word)), word == Elf::Ehdr::kMagic) {
      return ReadElf(*file, where, date, header, read_memory);
    }

    std::string_view header_string{
        reinterpret_cast<const char*>(header.data()),
        header.size(),
    };
    if (cpp20::starts_with(header_string, kArchiveMagic)) {
      return ReadArchive(*file, where, header, read_memory);
    }

    // If it's not a compressed file, we don't grok it.
    if (!DumpFile::IsCompressed(header)) {
      break;
    }

    // Start streaming decompression to deliver the uncompressed dump file.
    // Then iterate to read that (streaming) file.
    auto result = file->Decompress(where, header);
    if (result.is_error()) {
      return result.take_error();
    }
    file = result.value().get();
    dumps_.push_front(std::move(result).value());
  }
  return fitx::error(Error{"not an ELF or archive file", ZX_ERR_NOT_FILE});
}

fitx::result<Error> TaskHolder::JobTree::ReadElf(DumpFile& file, FileRange where, time_t date,
                                                 ByteView header, bool read_memory) {
  Elf::Ehdr ehdr;
  if (header.size() < sizeof(ehdr)) {
    return TruncatedDump();
  }
  memcpy(&ehdr, header.data(), sizeof(ehdr));
  if (!ehdr.Valid() || ehdr.phentsize() != sizeof(Elf::Phdr) ||
      ehdr.type != elfldltl::ElfType::kCore) {
    return fitx::error(Error{"ELF file is not a Zircon core dump", ZX_ERR_IO_DATA_INTEGRITY});
  }

  // Get the count of program headers.  Large counts use a special encoding
  // marked by PN_XNUM.  The 0th section header's sh_info is the real count.
  size_t phnum = ehdr.phnum;
  if (phnum == Elf::Ehdr::kPnXnum) {
    Elf::Shdr shdr;
    if (ehdr.shoff < sizeof(ehdr) || ehdr.shnum() == 0 || ehdr.shentsize() != sizeof(shdr)) {
      return fitx::error(Error{
          "invalid ELF section headers for PN_XNUM",
          ZX_ERR_IO_DATA_INTEGRITY,
      });
    }
    auto result = file.ReadEphemeral(where / FileRange{ehdr.shoff, sizeof(shdr)});
    if (result.is_error()) {
      return result.take_error();
    }
    if (result.value().size() < sizeof(shdr)) {
      return TruncatedDump();
    }
    memcpy(&shdr, result.value().data(), sizeof(shdr));
    phnum = shdr.info;
  }

  // Read the program headers.
  ByteView phdrs_bytes;
  if (ehdr.phoff > where.size || where.size / sizeof(Elf::Phdr) < phnum) {
    return TruncatedDump();
  } else {
    const size_t phdrs_size_bytes = phnum * sizeof(Elf::Phdr);
    auto result = file.ReadEphemeral(where / FileRange{ehdr.phoff, phdrs_size_bytes});
    if (result.is_error()) {
      return result.take_error();
    } else {
      phdrs_bytes = result.value();
    }
    if (phdrs_bytes.size() < phdrs_size_bytes) {
      // If it doesn't have all the phdrs, it won't have anything after them.
      return TruncatedDump();
    }
  }

  // Parse the program headers.  Note they occupy the ephemeral buffer
  // throughout the parsing loop, so it cannot use ReadEphemeral at all.

  // Process-wide notes will accumulate in the Process.
  Process process(*this);

  // Per-thread notes will accumulate in the thread until a new thread's first
  // note is seen.
  std::optional<Thread> thread;

  auto reify_thread = [&process, &thread]() {
    if (thread) {
      zx_koid_t koid = thread->koid();
      // Ignore duplicates here since they do no real harm.
      process.threads_.emplace_hint(process.threads_.end(), koid, std::move(*thread));
    }
  };

  // Parse a note segment.  Truncated notes do not cause an error.
  auto parse_notes = [&](FileRange notes) -> fitx::result<Error> {
    // Cap the segment size to what's available in the file.
    notes.size = std::min(notes.size, where.size - notes.offset);

    // Read the whole segment and keep it forever.
    ByteView bytes;
    if (auto result = file.ReadPermanent(where / notes); result.is_error()) {
      return result.take_error();
    } else {
      bytes = result.value();
    }

    // TODO(mcgrathr): Use elfldltl note parser.
    // Iterate through the notes.
    Elf::Nhdr nhdr;
    while (bytes.size() >= sizeof(nhdr)) {
      memcpy(&nhdr, bytes.data(), sizeof(nhdr));
      bytes.remove_prefix(sizeof(nhdr));
      auto name_bytes = bytes.substr(0, nhdr.namesz);
      if (bytes.size() < NoteAlign(nhdr.namesz)) {
        break;
      }
      bytes.remove_prefix(NoteAlign(nhdr.namesz));
      if (bytes.size() < NoteAlign(nhdr.namesz)) {
        break;
      }
      auto desc = bytes.substr(0, nhdr.descsz);
      if (bytes.size() < NoteAlign(nhdr.descsz)) {
        break;
      }
      bytes.remove_prefix(NoteAlign(nhdr.descsz));

      // All valid note names end with a NUL terminator.
      std::string_view name{
          reinterpret_cast<const char*>(name_bytes.data()),
          name_bytes.size(),
      };
      if (name.empty() || name.back() != '\0') {
        // Ignore bogus notes.  Could make them an error?
        continue;
      }
      name.remove_suffix(1);

      // Check for a system note.
      if (name == kSystemNoteName) {
        auto result = ReadSystemNote(desc);
        if (result.is_error()) {
          return result.take_error();
        }
        continue;
      }

      // Check for a process info note.
      if (name == kProcessInfoNoteName) {
        if (nhdr.type == ZX_INFO_HANDLE_BASIC) {
          zx_info_handle_basic_t info;
          if (desc.size() < sizeof(info)) {
            return CorruptedDump();
          }
          memcpy(&info, desc.data(), sizeof(info));

          // Validate the type because it's used for static_cast validation.
          if (info.type != ZX_OBJ_TYPE_PROCESS) {
            return CorruptedDump();
          }
        }
        auto result = AddNote(process.info_, nhdr.type(), desc);
        if (result.is_error()) {
          return result.take_error();
        }
        continue;
      }

      // Not a process info note.  Check for a process property note.
      if (name == kProcessPropertyNoteName) {
        auto result = AddNote(process.properties_, nhdr.type(), desc);
        if (result.is_error()) {
          return result.take_error();
        }
        continue;
      }

      // Not any kind of process note.  Check for a thread info note.
      if (name == kThreadInfoNoteName) {
        if (nhdr.type == ZX_INFO_HANDLE_BASIC) {
          // This marks the first note of a new thread.  Reify the last one.
          reify_thread();

          zx_info_handle_basic_t info;
          if (desc.size() < sizeof(info)) {
            return CorruptedDump();
          }
          memcpy(&info, desc.data(), sizeof(info));

          // Validate the type because it's used for static_cast validation.
          if (info.type != ZX_OBJ_TYPE_THREAD) {
            return CorruptedDump();
          }

          // Start recording a new thread.  This is the only place that
          // constructs new Thread objects, so every extant Thread has the
          // basic info.  But we don't validate that the KOID is not zero or a
          // duplicate.  Such bogons don't really do harm.  They will be
          // visible in the threads() list or to get_child calls using their
          // bogus KOIDs, even if they are never in the ZX_INFO_PROCESS_THREADS
          // list.  That behavior is inconsistent with a real live process but
          // it's consistent with the way the dump was actually written.
          //
          // This can't use emplace because the default constructor is private,
          // but the move constructor and move assignment operator are public.
          thread = {Thread{*this}};
        } else if (!thread) {
          return fitx::error(Error{
              "first thread info note is not ZX_INFO_HANDLE_BASIC",
              ZX_ERR_IO_DATA_INTEGRITY,
          });
        }

        auto result = AddNote(thread->info_, nhdr.type(), desc);
        if (result.is_error()) {
          return result.take_error();
        }
        continue;
      }

      // Not a thread info note.  Check for a thread property note.
      if (name == kThreadPropertyNoteName) {
        if (!thread) {
          return fitx::error(Error{
              "thread property note before thread ZX_INFO_HANDLE_BASIC note",
              ZX_ERR_IO_DATA_INTEGRITY,
          });
        }

        auto result = AddNote(thread->properties_, nhdr.type(), desc);
        if (result.is_error()) {
          return result.take_error();
        }
        continue;
      }

      // Not a thread property note.  Check for a thread state note.
      if (name == kThreadStateNoteName) {
        if (!thread) {
          return fitx::error(Error{
              "thread state note before thread ZX_INFO_HANDLE_BASIC note",
              ZX_ERR_IO_DATA_INTEGRITY,
          });
        }

        auto result = AddNote(thread->state_, nhdr.type(), desc);
        if (result.is_error()) {
          return result.take_error();
        }
        continue;
      }

      // Ignore unrecognized notes.  Could make them an error?
    }

    return fitx::ok();
  };

  // Validate a memory segment and add it to the memory map.
  auto add_segment = [&process](uint64_t vaddr, Process::Segment segment)  //
      -> fitx::result<Error> {
    ZX_DEBUG_ASSERT(segment.memsz > 0);
    if (!process.memory_.empty()) {
      const auto& [last_vaddr, last_segment] = *process.memory_.crbegin();
      ZX_DEBUG_ASSERT(last_segment.memsz > 0);
      if (vaddr <= last_vaddr) {
        return fitx::error(Error{
            "ELF core file PT_LOAD segments not in ascending address order",
            ZX_ERR_IO_DATA_INTEGRITY,
        });
      }
      if (vaddr < last_vaddr + last_segment.memsz) {
        return fitx::error(Error{
            "ELF core file PT_LOAD segments overlap",
            ZX_ERR_IO_DATA_INTEGRITY,
        });
      }
    }
    process.memory_.emplace_hint(process.memory_.end(), vaddr, segment);
    return fitx::ok();
  };

  while (!phdrs_bytes.empty()) {
    Elf::Phdr phdr;
    if (phdrs_bytes.size() < sizeof(phdr)) {
      return TruncatedDump();
    }
    memcpy(&phdr, phdrs_bytes.data(), sizeof(phdr));
    phdrs_bytes.remove_prefix(sizeof(phdr));
    if (phdr.type == elfldltl::ElfPhdrType::kNote && phdr.memsz() == 0 && phdr.filesz > 0) {
      // A non-allocated note segment should hold core notes.
      auto result = parse_notes({phdr.offset, phdr.filesz});
      if (result.is_error()) {
        return result.take_error();
      }
    } else if (read_memory && phdr.type == elfldltl::ElfPhdrType::kLoad && phdr.memsz > 0) {
      auto result = add_segment(phdr.vaddr, {phdr.offset, phdr.filesz, phdr.memsz});
      if (result.is_error()) {
        return result.take_error();
      }
    }
  }

  if (process.koid() == 0) {  // There was no ZX_INFO_HANDLE_BASIC note.
    return CorruptedDump();
  }

  // Looks like a valid dump.  Finish out the last pending thread.
  reify_thread();
  if (auto result = AttachProcess(std::move(process)); result.is_error()) {
    return result.take_error();
  }
  return fitx::ok();
}

fitx::result<Error> TaskHolder::JobTree::ReadArchive(DumpFile& file, FileRange archive,
                                                     ByteView header, bool read_memory) {
  // The first member's header comes immediately after kArchiveMagic.
  archive %= kArchiveMagic.size();
  header.remove_prefix(kArchiveMagic.size());

  if (archive.empty()) {
    return fitx::ok();
  }

  // This holds the current member's details.
  MemberHeader member{};
  FileRange contents{};

  // This parses the header into member and contents, and consumes them from
  // archive.
  auto parse = [&archive, &member, &contents](ByteView header)  //
      -> fitx::result<Error, bool> {
    if (auto result = ParseArchiveHeader(header); result.is_error()) {
      return result.take_error();
    } else {
      member = result.value();
    }
    archive %= sizeof(ar_hdr);
    if (member.size > archive.size) {
      return TruncatedDump();
    }
    contents = archive / member.size;
    archive %= member.size + (member.size & 1);
    return fitx::ok(true);
  };

  // This reads and parses the next header, consuming the member from archive.
  auto next = [&](bool probe = false) -> fitx::result<Error, bool> {
    ByteView header;
    if (auto result = file.ReadProbe(archive / sizeof(ar_hdr)); result.is_error()) {
      return result.take_error();
    } else {
      header = result.value();
    }
    if (probe && header.empty()) {
      return fitx::ok(false);
    }
    if (header.size() < sizeof(ar_hdr)) {
      return TruncatedDump();
    }
    return parse(header);
  };

  // Parse the first member header.
  if (auto result = parse(header); result.is_error()) {
    return result.take_error();
  }

  if (member.name == ar_hdr::kSymbolTableName) {
    // An archive symbol table was created by `ar`.  `gcore` won't add one.
    // Ignore it and read the next member.
    if (archive.empty()) {
      return fitx::ok();
    }
    if (auto result = next(); result.is_error()) {
      return result.take_error();
    }
  }

  std::string_view name_table;
  if (member.name == ar_hdr::kNameTableName) {
    // The special first member (or second member, if there was a symbol table)
    // is the long name table.
    if (auto result = file.ReadPermanent(contents); result.is_error()) {
      return result.take_error();
    } else {
      name_table = {
          reinterpret_cast<const char*>(result.value().data()),
          result.value().size(),
      };
    }
    if (archive.empty()) {
      return fitx::ok();
    }
    if (auto result = next(); result.is_error()) {
      return result.take_error();
    }
  }

  // Any note members will collect in this Job.
  Job job{*this};

  // Process one normal member.  It might be a note or an embedded dump file.
  auto handle_member = [&]() -> fitx::result<Error> {
    // Check for an info note.
    if (auto info = JobNoteName<zx_object_info_topic_t>(kJobInfoPrefix, member.name);
        info.is_error()) {
      return info.take_error();
    } else if (info.value()) {
      const zx_object_info_topic_t topic = *info.value();
      ByteView bytes;
      if (auto result = file.ReadPermanent(contents); result.is_error()) {
        return result.take_error();
      } else {
        bytes = result.value();
      }
      if (topic == ZX_INFO_HANDLE_BASIC) {
        zx_info_handle_basic_t basic_info;
        if (bytes.size() < sizeof(basic_info)) {
          return CorruptedDump();
        }
        memcpy(&basic_info, bytes.data(), sizeof(basic_info));

        // Validate the type because it's used for static_cast validation.
        if (basic_info.type != ZX_OBJ_TYPE_JOB) {
          return CorruptedDump();
        }
      }
      return AddNote(job.info_, topic, bytes);
    }

    // Not an info note.  Check for a property note.
    if (auto property = JobNoteName<uint32_t>(kJobPropertyPrefix, member.name);
        property.is_error()) {
      return property.take_error();
    } else if (property.value()) {
      auto result = file.ReadPermanent(contents);
      if (result.is_error()) {
        return result.take_error();
      }
      return AddNote(job.properties_, *property.value(), result.value());
    }

    // Check for a system note.
    if (member.name == kSystemNoteName) {
      auto result = file.ReadEphemeral(contents);
      if (result.is_error()) {
        return result.take_error();
      }
      return ReadSystemNote(result.value());
    }

    // This member file is not a job note.  It's an embedded dump file.
    return Read(file, read_memory, contents, member.date);
  };

  // Iterate through the normal members.
  while (true) {
    // Specially-encoded member names are actually indices into the name table.
    if (!HandleLongName(name_table, member)) {
      return CorruptedDump();
    }

    if (auto result = handle_member(); result.is_error()) {
      return result.take_error();
    }

    if (archive.empty()) {
      break;
    }

    if (auto result = next(true); result.is_error()) {
      return result.take_error();
    } else if (!result.value()) {
      break;
    }
  }

  // End of the archive.  Reify the job.
  if (job.koid() != ZX_KOID_INVALID) {
    // Looks like a valid job.
    auto result = AttachJob(std::move(job));
    if (result.is_error()) {
      return result.take_error();
    }
    return fitx::ok();
  }

  if (job.info_.empty() && job.properties_.empty()) {
    // This was just a plain archive, not actually a job archive at all.
    return fitx::ok();
  }

  // This job archive had some notes but no ZX_INFO_HANDLE_BASIC note.
  return CorruptedDump();
}

fitx::result<Error> TaskHolder::JobTree::ReadSystemNote(ByteView data) {
  // If it's already been collected, then ignore new data.
  if (system_.IsObject()) {
    return fitx::ok();
  }

  std::string_view sv{reinterpret_cast<const char*>(data.data()), data.size()};
  StringViewStream stream{sv};
  system_.ParseStream(stream);

  return fitx::ok();
}

const rapidjson::Value* TaskHolder::JobTree::GetSystemJsonData(const char* key) const {
  if (system_.IsObject()) {
    auto it = system_.FindMember(key);
    if (it != system_.MemberEnd()) {
      return &it->value;
    }
  }
  return nullptr;
}

template <>
std::string_view TaskHolder::JobTree::GetSystemData<std::string_view>(const char* key) const {
  const rapidjson::Value* value = GetSystemJsonData(key);
  if (!value || !value->IsString()) {
    return {};
  }
  return {value->GetString(), value->GetStringLength()};
}

template <>
uint32_t TaskHolder::JobTree::GetSystemData<uint32_t>(const char* key) const {
  const rapidjson::Value* value = GetSystemJsonData(key);
  return !value              ? 0
         : value->IsUint()   ? value->GetUint()
         : value->IsNumber() ? static_cast<uint32_t>(value->GetDouble())
                             : 0;
}

template <>
uint64_t TaskHolder::JobTree::GetSystemData<uint64_t>(const char* key) const {
  const rapidjson::Value* value = GetSystemJsonData(key);
  return !value              ? 0
         : value->IsUint64() ? value->GetUint64()
         : value->IsNumber() ? static_cast<uint64_t>(value->GetDouble())
                             : 0;
}

std::string_view TaskHolder::system_get_version_string() const {
  return tree_->GetSystemData<std::string_view>("version_string");
}

uint32_t TaskHolder::system_get_dcache_line_size() const {
  return tree_->GetSystemData<uint32_t>("dcache_line_size");
}

uint32_t TaskHolder::system_get_num_cpus() const {
  return tree_->GetSystemData<uint32_t>("num_cpus");
}

uint64_t TaskHolder::system_get_page_size() const {
  return tree_->GetSystemData<uint64_t>("page_size");
}

uint64_t TaskHolder::system_get_physmem() const {
  return tree_->GetSystemData<uint64_t>("physmem");
}

}  // namespace zxdump
