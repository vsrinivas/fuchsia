// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <lib/zxdump/dump.h>
#include <lib/zxdump/fd-writer.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <fbl/unique_fd.h>
#include <task-utils/get.h>

namespace {

using namespace std::literals;

constexpr std::string_view kOutputPrefix = "core."sv;
constexpr std::string_view kArchiveSuffix = ".a"sv;

// Command-line flags controlling the dump are parsed into this object, which
// is passed around to the methods affected by policy choices.
struct Flags {
  std::string OutputFile(zx_koid_t pid, bool outer = true, std::string_view suffix = {}) const {
    std::string filename{outer ? output_prefix_ : kOutputPrefix};
    filename += std::to_string(pid);
    filename += suffix;
    return filename;
  }

  time_t Date() const { return archive_member_date_ ? time(nullptr) : 0; }

  std::string_view output_prefix_ = kOutputPrefix;
  size_t limit_ = zxdump::DefaultLimit();
  bool dump_memory_ = true;
  bool collect_threads_ = true;
  bool collect_job_children_ = true;
  bool collect_job_processes_ = true;
  bool flatten_jobs_ = false;
  bool archive_member_date_ = true;
};

// This handles writing a single output file, and removing that output file if
// the dump is aborted before `Ok(true)` is called.
class Writer : public zxdump::FdWriter {
 public:
  Writer(fbl::unique_fd fd, std::string filename)
      : zxdump::FdWriter{std::move(fd)}, filename_{std::move(filename)} {}

  // Write errors from errno use the file name.
  void Error(std::string_view op) {
    ZX_DEBUG_ASSERT(!op.empty());
    std::string_view fn = filename_;
    if (fn.empty()) {
      fn = "<stdout>"sv;
    }
    std::cerr << fn << ": "sv << op << ": "sv << strerror(errno) << std::endl;
  }

  // Called with true if the output file should be preserved at destruction.
  bool Ok(bool ok) {
    if (ok) {
      filename_.clear();
    }
    return ok;
  }

  ~Writer() {
    if (!filename_.empty()) {
      remove(filename_.c_str());
    }
  }

 private:
  std::string filename_;
};

// This is the base class of ProcessDumper and JobDumper; the object
// handles collecting and producing the dump for one process or one job.
// The Writer and Flags objects get passed in to control the details
// and of the dump and where it goes.
class DumperBase {
 public:
  static fitx::result<zxdump::Error, zxdump::SegmentDisposition> PruneAll(
      zxdump::SegmentDisposition segment, const zx_info_maps_t& mapping, const zx_info_vmo_t& vmo) {
    segment.filesz = 0;
    return fitx::ok(segment);
  }

  static fitx::result<zxdump::Error, zxdump::SegmentDisposition> PruneDefault(
      zxdump::SegmentDisposition segment, const zx_info_maps_t& mapping, const zx_info_vmo_t& vmo) {
    if (mapping.u.mapping.committed_pages == 0 &&   // No private RAM here,
        vmo.parent_koid == ZX_KOID_INVALID &&       // and none shared,
        !(vmo.flags & ZX_INFO_VMO_PAGER_BACKED)) {  // and no backing store.
      // Since it's not pager-backed, there isn't data hidden in backing
      // store.  If we read this, it would just be zero-fill anyway.
      segment.filesz = 0;
    }

    // TODO(mcgrathr): for now, dump everything else.

    return fitx::ok(segment);
  }

  // Read errors from syscalls use the PID (or job KOID).
  void Error(const zxdump::Error& error) const {
    std::cerr << koid_ << ": "sv << error << std::endl;
  }

  zx_koid_t koid() const { return koid_; }

 protected:
  constexpr explicit DumperBase(zx_koid_t koid) : koid_(koid) {}

 private:
  zx_koid_t koid_ = ZX_KOID_INVALID;
};

class ProcessDumper : public DumperBase {
 public:
  ProcessDumper(zx::process process, zx_koid_t pid)
      : DumperBase{pid}, dumper_{std::move(process)} {}

  auto OutputFile(const Flags& flags, bool outer = true) const {
    return flags.OutputFile(koid(), outer);
  }

  // Phase 1: Collect underpants!
  std::optional<size_t> Collect(const Flags& flags) {
    zxdump::SegmentCallback prune = PruneAll;
    if (flags.dump_memory_) {
      // TODO(mcgrathr): more filtering switches
      prune = PruneDefault;
    }

    if (flags.collect_threads_) {
      auto result = dumper_.SuspendAndCollectThreads();
      if (result.is_error()) {
        Error(result.error_value());
        return std::nullopt;
      }
    }

    auto result = dumper_.CollectProcess(std::move(prune), flags.limit_);
    if (result.is_error()) {
      Error(result.error_value());
      return std::nullopt;
    }

    return result.value();
  }

  // Phase 2: ???
  bool Dump(Writer& writer, const Flags& flags) {
    // File offset calculations start fresh in each ET_CORE file.
    writer.ResetOffset();

    // Now gather the accumulated header data first: not including the memory.
    // These iovecs will point into storage in the ProcessDump object itself.
    size_t total;
    if (auto result = dumper_.DumpHeaders(writer.AccumulateFragmentsCallback(), flags.limit_);
        result.is_error()) {
      Error(result.error_value());
      return false;
    } else {
      total = result.value();
    }

    if (total > flags.limit_) {
      errno = EFBIG;
      writer.Error("not written");
      return false;
    }

    // All the fragments gathered above get written at once.
    if (auto result = writer.WriteFragments(); result.is_error()) {
      writer.Error(result.error_value());
      return false;
    }

    // Stream the memory out via a temporary buffer that's reused repeatedly
    // for each callback.
    if (auto memory = dumper_.DumpMemory(writer.WriteCallback(), flags.limit_); memory.is_error()) {
      Error(memory.error_value());
      return false;
    }

    return true;
  }

 private:
  zxdump::ProcessDump<zx::process> dumper_;
};

// JobDumper handles dumping one job archive, either hierarchical or flattened.
class JobDumper : public DumperBase {
 public:
  JobDumper(zx::job job, zx_koid_t koid) : DumperBase{koid}, dumper_{std::move(job)} {}

  auto OutputFile(const Flags& flags, bool outer = true) const {
    return flags.OutputFile(koid(), outer, kArchiveSuffix);
  }

  // The job dumper records the date of collection to use in the stub archive
  // member headers.  If the job archive is collected inside another archive,
  // this will also be the date in the member header for the nested archive.
  time_t date() const { return date_; }

  auto* operator->() { return &dumper_; }

  // Collect the job-wide data and reify the lists of children and processes.
  std::optional<size_t> Collect(const Flags& flags) {
    date_ = flags.Date();
    size_t size;
    if (auto result = dumper_.CollectJob(); result.is_error()) {
      Error(result.error_value());
      return std::nullopt;
    } else {
      size = result.value();
    }
    if (flags.collect_job_children_) {
      if (auto result = dumper_.CollectChildren(); result.is_error()) {
        Error(result.error_value());
        return std::nullopt;
      } else {
        children_ = std::move(result.value());
      }
    }
    if (flags.collect_job_processes_) {
      if (auto result = dumper_.CollectProcesses(); result.is_error()) {
        Error(result.error_value());
        return std::nullopt;
      } else {
        processes_ = std::move(result.value());
      }
    }
    return size;
  }

  // Dump the job archive: first dump the stub archive, and then collect and
  // dump each process and each child.
  bool Dump(Writer& writer, const Flags& flags);

 private:
  using JobDump = zxdump::JobDump<zx::job>;
  class CollectedJob;

  JobDumper(JobDump job, zx_koid_t koid) : DumperBase{koid}, dumper_{std::move(job)} {}

  bool DumpHeaders(Writer& writer, const Flags& flags) {
    // File offset calculations start fresh in each archive.
    writer.ResetOffset();

    if (auto result = dumper_.DumpHeaders(writer.AccumulateFragmentsCallback(), date_);
        result.is_error()) {
      Error(result.error_value());
      return false;
    }

    auto result = writer.WriteFragments();
    if (result.is_error()) {
      writer.Error(result.error_value());
      return false;
    }

    return true;
  }

  static size_t MemberHeaderSize() { return JobDump::MemberHeaderSize(); }

  static bool DumpMemberHeader(Writer& writer, std::string_view name, size_t size, time_t mtime) {
    // File offset calculations start fresh with each member.
    writer.ResetOffset();
    auto write = writer.WriteCallback();
    auto result = JobDump::DumpMemberHeader(write, 0, name, size, mtime);
    if (result.is_error()) {
      writer.Error(*result.error_value().dump_error_);
    }
    return result.is_ok();
  }

  JobDump dumper_;
  JobDump::JobVector children_;
  JobDump::ProcessVector processes_;
  time_t date_ = 0;
};

// When dumping an hierarchical job archive, a CollectedJob object supports
// DeepCollect, that populates a tree of CollectedJob and CollectedProcess
// objects before the whole tree is dumped en masse.
class JobDumper::CollectedJob {
 public:
  CollectedJob(CollectedJob&&) = default;
  CollectedJob& operator=(CollectedJob&&) = default;

  explicit CollectedJob(JobDumper dumper) : dumper_(std::move(dumper)) {}

  // This is false either if the original Collect failed and this is an empty
  // object; or if any process or child collection failed so the archive is
  // still valid but just omits some processes and/or children.
  bool ok() const { return ok_; }

  // This includes the whole size of the job archive itself plus its
  // own member header as a member of its parent archive.
  size_t size_bytes() const { return MemberHeaderSize() + content_size_; }

  time_t date() const { return dumper_.date(); }

  // Returns true if the job itself was collected.
  // Later ok() indicates if any process or child collection failed.
  bool DeepCollect(const Flags& flags) {
    // Collect the job itself.
    if (auto collected_size = dumper_.Collect(flags)) {
      content_size_ += *collected_size;

      // Collect all its processes and children.
      for (auto& [process, pid] : dumper_.processes_) {
        CollectProcess(std::move(process), pid, flags);
      }
      for (auto& [job, koid] : dumper_.children_) {
        CollectJob(std::move(job), koid, flags);
      }
      return true;
    }
    ok_ = false;
    return false;
  }

  bool Dump(Writer& writer, const Flags& flags) {
    // First dump the member header for this archive as a member of its parent.
    // Then dump the "stub archive" describing the job itself.
    if (!DumpMemberHeader(writer, dumper_.OutputFile(flags, false), content_size_, date()) ||
        !dumper_.DumpHeaders(writer, flags)) {
      ok_ = false;
    } else {
      for (auto& process : processes_) {
        // Each CollectedProcess dumps its own member header and ET_CORE file.
        ok_ = process.Dump(writer, flags) && ok_;
      }
      for (auto& job : children_) {
        // Recurse on each child to dump its own member header and job archive.
        ok_ = job.Dump(writer, flags) && ok_;
      }
    }
    return ok_;
  }

 private:
  // At the leaves of the tree are processes still suspended after collection.
  class CollectedProcess {
   public:
    CollectedProcess(CollectedProcess&&) = default;
    CollectedProcess& operator=(CollectedProcess&&) = default;

    // Constructed with the process dumper and the result of Collect on it.
    CollectedProcess(ProcessDumper&& dumper, size_t size, time_t date)
        : dumper_(std::move(dumper)), content_size_(size), date_(date) {}

    // This includes the whole size of the ET_CORE file itself plus its
    // own member header as a member of its parent archive.
    size_t size_bytes() const { return MemberHeaderSize() + content_size_; }

    time_t date() const { return date_; }

    // Dump the member header and then the ET_CORE file contents.
    bool Dump(Writer& writer, const Flags& flags) {
      return DumpMemberHeader(writer, dumper_.OutputFile(flags, false), content_size_, date()) &&
             dumper_.Dump(writer, flags);
    }

   private:
    ProcessDumper dumper_;
    size_t content_size_ = 0;
    time_t date_ = 0;
  };

  void CollectProcess(zx::process process, zx_koid_t pid, const Flags& flags) {
    ProcessDumper dump{std::move(process), pid};
    time_t dump_date = flags.Date();
    if (auto collected_size = dump.Collect(flags)) {
      CollectedProcess core_file{std::move(dump), *collected_size, dump_date};
      content_size_ += core_file.size_bytes();
      processes_.push_back(std::move(core_file));
    } else {
      ok_ = false;
    }
  }

  void CollectJob(zx::job job, zx_koid_t koid, const Flags& flags) {
    CollectedJob archive{{std::move(job), koid}};
    if (archive.DeepCollect(flags)) {
      content_size_ += archive.size_bytes();
      children_.push_back(std::move(archive));
    }
    // The job archive reports not OK if it was collected but omits some dumps.
    ok_ = archive.ok() && ok_;
  }

  JobDumper dumper_;
  std::vector<CollectedProcess> processes_;
  std::vector<CollectedJob> children_;
  size_t content_size_ = 0;
  bool ok_ = true;
};

bool JobDumper::Dump(Writer& writer, const Flags& flags) {
  if (!DumpHeaders(writer, flags)) {
    return false;
  }

  bool ok = true;
  for (auto& [process, pid] : processes_) {
    // Collect the process and thus discover the ET_CORE file size.
    ProcessDumper process_dump{std::move(process), pid};
    time_t process_dump_date = flags.Date();
    if (auto collected_size = process_dump.Collect(flags)) {
      // Dump the member header, now complete with size.
      if (!DumpMemberHeader(writer, process_dump.OutputFile(flags, false),  //
                            *collected_size, process_dump_date)) {
        // Bail early for a write error, since later writes would fail too.
        return false;
      }
      // Now dump the member contents, the ET_CORE file for the process.
      ok = process_dump.Dump(writer, flags) && ok;
    }
  }

  for (auto& [job, koid] : children_) {
    if (flags.flatten_jobs_) {
      // Collect just this job first.
      JobDumper child{std::move(job), koid};
      auto collected_job_size = child.Collect(flags);
      ok = collected_job_size &&
           // Stream out the member header for just the stub archive alone.
           DumpMemberHeader(writer, child.OutputFile(flags, false),  //
                            *collected_job_size, child.date()) &&
           // Now recurse to dump the stub archive followed by process and
           // child members.  Since the member header for the inner archive
           // only covers the stub archive, these become members in the outer
           // (flat) archive rather than members of the inner job archive.
           // Another inner recursion will do the same thing, so all the
           // recursions stream out a single flat archive.
           child.Dump(writer, flags) && ok;
    } else {
      // Pre-collect the whole job tree and thus discover the archive size.
      // The pre-collected archive dumps its own member header first.
      CollectedJob archive{{std::move(job), koid}};
      ok = archive.DeepCollect(flags) && archive.Dump(writer, flags) && ok;
    }
  }
  return ok;
}

fbl::unique_fd CreateOutputFile(const std::string& outfile) {
  fbl::unique_fd fd{open(outfile.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_EXCL, 0666)};
  if (!fd) {
    perror(outfile.c_str());
  }
  return fd;
}

// Phase 3: Profit!
template <typename Dumper>
bool WriteDump(Dumper dumper, const Flags& flags) {
  std::string outfile = dumper.OutputFile(flags);
  fbl::unique_fd fd = CreateOutputFile(outfile);
  if (!fd) {
    return false;
  }
  Writer writer{std::move(fd), std::move(outfile)};
  return writer.Ok(dumper.Collect(flags) && dumper.Dump(writer, flags));
}

// "Dump" a job tree by actually just making separate dumps of each process.
// We only use the JobDumper to find the processes and/or children.
bool WriteManyCoreFiles(JobDumper dumper, const Flags& flags) {
  bool ok = true;

  if (flags.collect_job_processes_) {
    if (auto result = dumper->CollectProcesses(); result.is_error()) {
      dumper.Error(result.error_value());
      ok = false;
    } else {
      for (auto& [process, pid] : result.value()) {
        ok = WriteDump(ProcessDumper{std::move(process), pid}, flags) && ok;
      }
    }
  }

  if (flags.collect_job_children_) {
    if (auto result = dumper->CollectChildren(); result.is_error()) {
      dumper.Error(result.error_value());
      ok = false;
    } else {
      for (auto& [job, jid] : result.value()) {
        ok = WriteManyCoreFiles({std::move(job), jid}, flags) && ok;
      }
    }
  }

  return ok;
}

constexpr const char kOptString[] = "hlo:mtcpJjfDU";
constexpr const option kLongOpts[] = {
    {"help", no_argument, nullptr, 'h'},                 //
    {"limit", required_argument, nullptr, 'l'},          //
    {"output-prefix", required_argument, nullptr, 'o'},  //
    {"exclude-memory", no_argument, nullptr, 'm'},       //
    {"no-threads", no_argument, nullptr, 't'},           //
    {"no-children", no_argument, nullptr, 'c'},          //
    {"no-processes", no_argument, nullptr, 'p'},         //
    {"jobs", no_argument, nullptr, 'J'},                 //
    {"job-archive", no_argument, nullptr, 'j'},          //
    {"flat-job-archive", no_argument, nullptr, 'f'},     //
    {"no-date", no_argument, nullptr, 'D'},              //
    {"date", no_argument, nullptr, 'U'},                 //
    {nullptr, no_argument, nullptr, 0},                  //
};

}  // namespace

int main(int argc, char** argv) {
  Flags flags;
  bool allow_jobs = false;
  auto handle_job = WriteManyCoreFiles;

  auto usage = [&](int status = EXIT_FAILURE) {
    std::cerr << "Usage: " << argv[0] << R"""( [SWITCHES...] PID...

    --help, -h                         print this message
    --output-prefix=PREFIX, -o PREFIX  write <PREFIX><PID>, not core.<PID>
    --limit=BYTES, -l BYTES            truncate output to BYTES per process
    --exclude-memory, -M               exclude all process memory from dumps
    --no-threads, -t                   collect only memory, threads left to run
    --jobs, -J                         allow PIDs to be job KOIDs instead
    --job-archive, -j                  write job archives, not process dumps
    --flat-job-archive, -f             write flattened job archives
    --no-children, -c                  don't recurse to child jobs
    --no-processes, -p                 don't dump processes found in jobs
    --no-date, -D                      don't put dates into job archives
    --date, -U                         do put dates into job archives (default)

By default, each PID must be the KOID of a process.

With --jobs, the KOID of a job is allowed.  Each process gets a separate dump
named for its individual PID.

With --job-archive, the KOID of a job is allowed.  Each job is dumped into a
job archive named <PREFIX><KOID>.a instead of producing per-process dump files.
If child jobs are dumped they become `core.<KOID>.a` archive members that are
themselves job archives.

With --no-children, don't recurse into child jobs of a job.
With --no-process, don't dump processes within a job, only its child jobs.
Using --no-process with --jobs rather than --job-archive means no dumps are
produced from job KOIDs at all, but valid job KOIDs are ignored rather than
causing errors.

Each argument is dumped synchronously before processing the next argument.
Errors dumping each process are reported and cause a failing exit status at
the end of the run, but do not prevent additional processes from being dumped.
Without --no-threads, each process is held suspended while being dumped.
Processes within a job are dumped serially.  When dumping a child job inside a
job archive, all processes inside that whole subtree are held suspended until
the whole child job archive is dumped.

With --flat-job-archive, child job archives inside a job archive are instead
"stub" job archives that only describe the job itself.  A child job's process
and (grand)child job dumps are all included directly in the outer "flat" job
archive.  In this mode, only one process is held suspended at a time.

Jobs are always dumped while they continue to run and may omit new processes
or child jobs created after the dump collection begins.  Job dumps may report
process or child job KOIDs that were never dumped if they died during
collection.
)""";
    return status;
  };

  while (true) {
    switch (getopt_long(argc, argv, kOptString, kLongOpts, nullptr)) {
      case -1:
        // This ends the loop.  All other cases continue (or return).
        break;

      case 'D':
        flags.archive_member_date_ = false;
        continue;

      case 'U':
        flags.archive_member_date_ = true;
        continue;

      case 'o':
        flags.output_prefix_ = optarg;
        continue;

      case 'l': {
        char* p;
        flags.limit_ = strtoul(optarg, &p, 0);
        if (*p != '\0') {
          return usage();
        }
        continue;
      }

      case 'm':
        flags.dump_memory_ = false;
        continue;

      case 't':
        flags.collect_threads_ = false;
        continue;

      case 'f':
        flags.flatten_jobs_ = true;
        [[fallthrough]];

      case 'j':
        handle_job = WriteDump<JobDumper>;
        [[fallthrough]];

      case 'J':
        allow_jobs = true;
        continue;

      case 'c':
        flags.collect_job_children_ = false;
        continue;

      case 'p':
        flags.collect_job_processes_ = false;
        continue;

      default:
        return usage(EXIT_SUCCESS);
    }
    break;
  }

  if (optind == argc) {
    return usage();
  }

  int exit_status = EXIT_SUCCESS;
  for (int i = optind; i < argc; ++i) {
    char* p;
    zx_koid_t pid = strtoul(argv[i], &p, 0);
    if (*p != '\0') {
      std::cerr << "Not a PID or job KOID: " << argv[i] << std::endl;
      return usage();
    }

    zx_obj_type_t type;
    zx_handle_t handle;
    zx_status_t status = get_task_by_koid(pid, &type, &handle);
    if (status != ZX_OK) {
      std::cerr << pid << ": " << zx_status_get_string(status) << std::endl;
      status = EXIT_FAILURE;
      continue;
    }

    switch (type) {
      case ZX_OBJ_TYPE_PROCESS:
        if (!WriteDump(ProcessDumper{zx::process{handle}, pid}, flags)) {
          exit_status = EXIT_FAILURE;
        }
        break;

      case ZX_OBJ_TYPE_JOB:
        if (allow_jobs) {
          if (!handle_job(JobDumper{zx::job{handle}, pid}, flags)) {
            exit_status = EXIT_FAILURE;
          }
          break;
        }
        [[fallthrough]];

      default:
        std::cerr << pid << ": KOID is not a process\n";
        zx_handle_close(handle);
        exit_status = EXIT_FAILURE;
        break;
    }
  }

  return exit_status;
}
