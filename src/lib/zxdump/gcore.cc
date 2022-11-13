// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <getopt.h>
#include <lib/zxdump/dump.h>
#include <lib/zxdump/fd-writer.h>
#include <lib/zxdump/task.h>
#include <lib/zxdump/zstd-writer.h>
#include <zircon/assert.h>
#include <zircon/status.h>

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

#include <fbl/unique_fd.h>

namespace {

using namespace std::literals;

constexpr std::string_view kOutputPrefix = "core."sv;
constexpr std::string_view kArchiveSuffix = ".a"sv;
constexpr std::string_view kZstdSuffix = ".zst"sv;

// Command-line flags controlling the dump are parsed into this object, which
// is passed around to the methods affected by policy choices.
struct Flags {
  std::string OutputFile(zx_koid_t pid, bool outer = true, std::string_view suffix = {}) const {
    std::string filename{outer ? output_prefix : kOutputPrefix};
    filename += std::to_string(pid);
    filename += suffix;
    if (outer && zstd) {
      filename += kZstdSuffix;
    }
    return filename;
  }

  time_t Date() const { return record_date ? time(nullptr) : 0; }

  std::string_view output_prefix = kOutputPrefix;
  size_t limit = zxdump::DefaultLimit();
  bool dump_memory = true;
  bool collect_system = false;
  bool repeat_system = false;
  bool collect_kernel = false;
  bool repeat_kernel = false;
  bool collect_threads = true;
  bool collect_job_children = true;
  bool collect_job_processes = true;
  bool flatten_jobs = false;
  bool record_date = true;
  bool zstd = false;
};

// This handles writing a single output file, and removing that output file if
// the dump is aborted before `Ok(true)` is called.
class Writer {
 public:
  using error_type = zxdump::FdWriter::error_type;

  Writer() = delete;

  Writer(fbl::unique_fd fd, std::string filename, bool zstd)
      : writer_{zstd ? WhichWriter{zxdump::ZstdWriter(std::move(fd))}
                     : WhichWriter{zxdump::FdWriter(std::move(fd))}},
        filename_{std::move(filename)} {}

  auto AccumulateFragmentsCallback() {
    return std::visit(
        [](auto& writer)
            -> fit::function<fit::result<error_type>(size_t offset, zxdump::ByteView data)> {
          return writer.AccumulateFragmentsCallback();
        },
        writer_);
  }

  auto WriteFragments() {
    return std::visit(
        [](auto& writer) -> fit::result<error_type, size_t> { return writer.WriteFragments(); },
        writer_);
  }

  auto WriteCallback() {
    return std::visit(
        [](auto& writer)
            -> fit::function<fit::result<error_type>(size_t offset, zxdump::ByteView data)> {
          return writer.WriteCallback();
        },
        writer_);
  }

  void ResetOffset() {
    std::visit([](auto& writer) { writer.ResetOffset(); }, writer_);
  }

  // Write errors from errno use the file name.
  void Error(zxdump::FdError error) {
    std::string_view fn = filename_;
    if (fn.empty()) {
      fn = "<stdout>"sv;
    }
    std::cerr << fn << ": "sv << error << std::endl;
  }

  // Called with true if the output file should be preserved at destruction.
  bool Ok(bool ok) {
    if (ok) {
      if (auto writer = std::get_if<zxdump::ZstdWriter>(&writer_)) {
        auto result = writer->Finish();
        if (result.is_error()) {
          Error(result.error_value());
          return false;
        }
      }
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
  using WhichWriter = std::variant<zxdump::FdWriter, zxdump::ZstdWriter>;

  WhichWriter writer_;
  std::string filename_;
};

// This is the base class of ProcessDumper and JobDumper; the object
// handles collecting and producing the dump for one process or one job.
// The Writer and Flags objects get passed in to control the details
// and of the dump and where it goes.
class DumperBase {
 public:
  static fit::result<zxdump::Error, zxdump::SegmentDisposition> PruneAll(
      zxdump::SegmentDisposition segment, const zx_info_maps_t& mapping, const zx_info_vmo_t& vmo) {
    segment.filesz = 0;
    return fit::ok(segment);
  }

  static fit::result<zxdump::Error, zxdump::SegmentDisposition> PruneDefault(
      zxdump::SegmentDisposition segment, const zx_info_maps_t& mapping, const zx_info_vmo_t& vmo) {
    if (mapping.u.mapping.committed_pages == 0 &&   // No private RAM here,
        vmo.parent_koid == ZX_KOID_INVALID &&       // and none shared,
        !(vmo.flags & ZX_INFO_VMO_PAGER_BACKED)) {  // and no backing store.
      // Since it's not pager-backed, there isn't data hidden in backing
      // store.  If we read this, it would just be zero-fill anyway.
      segment.filesz = 0;
    }

    // TODO(mcgrathr): for now, dump everything else.

    return fit::ok(segment);
  }

  // Read errors from syscalls use the PID (or job KOID).
  void Error(const zxdump::Error& error) const {
    std::cerr << koid_ << ": "sv << error << std::endl;
  }

  zx_koid_t koid() const { return koid_; }

  time_t ClockIn(const Flags& flags) { return 0; }

 protected:
  constexpr explicit DumperBase(zx_koid_t koid) : koid_(koid) {}

 private:
  zx_koid_t koid_ = ZX_KOID_INVALID;
};

// This does the Collect* calls that are common to ProcessDumper and JobDumper.
constexpr auto CollectCommon = [](const Flags& flags, bool top,
                                  auto& dumper) -> fit::result<zxdump::Error> {
  if (flags.collect_system && (top || flags.repeat_system)) {
    auto result = dumper.CollectSystem();
    if (result.is_error()) {
      return result.take_error();
    }
  }

  if (flags.collect_kernel && (top || flags.repeat_kernel)) {
    auto resource = zxdump::GetRootResource();
    if (resource.is_error()) {
      return resource.take_error();
    }

    auto result = dumper.CollectKernel(resource->borrow());
    if (result.is_error()) {
      return result.take_error();
    }
  }

  return fit::ok();
};

class ProcessDumper : public DumperBase {
 public:
  ProcessDumper(zx::process process, zx_koid_t pid)
      : DumperBase{pid}, dumper_{std::move(process)} {}

  auto OutputFile(const Flags& flags, bool outer = true) const {
    return flags.OutputFile(koid(), outer);
  }

  time_t ClockIn(const Flags& flags) {
    time_t dump_date = flags.Date();
    if (dump_date != 0) {
      dumper_.set_date(dump_date);
    }
    return dump_date;
  }

  // Phase 1: Collect underpants!
  std::optional<size_t> Collect(const Flags& flags, bool top) {
    zxdump::SegmentCallback prune = PruneAll;
    if (flags.dump_memory) {
      // TODO(mcgrathr): more filtering switches
      prune = PruneDefault;
    }

    if (flags.collect_threads) {
      auto result = dumper_.SuspendAndCollectThreads();
      if (result.is_error()) {
        Error(result.error_value());
        return std::nullopt;
      }
    }

    if (auto result = CollectCommon(flags, top, dumper_); result.is_error()) {
      Error(result.error_value());
      return std::nullopt;
    }

    auto result = dumper_.CollectProcess(std::move(prune), flags.limit);
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
    if (auto result = dumper_.DumpHeaders(writer.AccumulateFragmentsCallback(), flags.limit);
        result.is_error()) {
      Error(result.error_value());
      return false;
    } else {
      total = result.value();
    }

    if (total > flags.limit) {
      writer.Error({.op_ = "not written"sv, .error_ = EFBIG});
      return false;
    }

    // All the fragments gathered above get written at once.
    if (auto result = writer.WriteFragments(); result.is_error()) {
      writer.Error(result.error_value());
      return false;
    }

    // Stream the memory out via a temporary buffer that's reused repeatedly
    // for each callback.
    if (auto memory = dumper_.DumpMemory(writer.WriteCallback(), flags.limit); memory.is_error()) {
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
  std::optional<size_t> Collect(const Flags& flags, bool top) {
    date_ = flags.Date();
    if (auto result = CollectCommon(flags, top, dumper_); result.is_error()) {
      Error(result.error_value());
      return std::nullopt;
    }
    size_t size;
    if (auto result = dumper_.CollectJob(); result.is_error()) {
      Error(result.error_value());
      return std::nullopt;
    } else {
      size = result.value();
    }
    if (flags.collect_job_children) {
      if (auto result = dumper_.CollectChildren(); result.is_error()) {
        Error(result.error_value());
        return std::nullopt;
      } else {
        children_ = std::move(result.value());
      }
    }
    if (flags.collect_job_processes) {
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
    auto result = JobDump::DumpMemberHeader(writer.WriteCallback(), 0, name, size, mtime);
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
    dumper_.ClockIn(flags);
    if (auto collected_size = dumper_.Collect(flags, false)) {
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
    time_t dump_date = dump.ClockIn(flags);
    if (auto collected_size = dump.Collect(flags, false)) {
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
    time_t process_dump_date = process_dump.ClockIn(flags);
    if (auto collected_size = process_dump.Collect(flags, false)) {
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
    if (flags.flatten_jobs) {
      // Collect just this job first.
      JobDumper child{std::move(job), koid};
      auto collected_job_size = child.Collect(flags, false);
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
  Writer writer{std::move(fd), std::move(outfile), flags.zstd};
  dumper.ClockIn(flags);
  return writer.Ok(dumper.Collect(flags, true) && dumper.Dump(writer, flags));
}

// "Dump" a job tree by actually just making separate dumps of each process.
// We only use the JobDumper to find the processes and/or children.
bool WriteManyCoreFiles(JobDumper dumper, const Flags& flags) {
  bool ok = true;

  if (flags.collect_job_processes) {
    if (auto result = dumper->CollectProcesses(); result.is_error()) {
      dumper.Error(result.error_value());
      ok = false;
    } else {
      for (auto& [process, pid] : result.value()) {
        ok = WriteDump(ProcessDumper{std::move(process), pid}, flags) && ok;
      }
    }
  }

  if (flags.collect_job_children) {
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

constexpr const char kOptString[] = "hlo:zamtcpJjfDUsSkK";
constexpr const option kLongOpts[] = {
    {"help", no_argument, nullptr, 'h'},                 //
    {"limit", required_argument, nullptr, 'l'},          //
    {"output-prefix", required_argument, nullptr, 'o'},  //
    {"zstd", no_argument, nullptr, 'z'},                 //
    {"exclude-memory", no_argument, nullptr, 'm'},       //
    {"no-threads", no_argument, nullptr, 't'},           //
    {"no-children", no_argument, nullptr, 'c'},          //
    {"no-processes", no_argument, nullptr, 'p'},         //
    {"jobs", no_argument, nullptr, 'J'},                 //
    {"job-archive", no_argument, nullptr, 'j'},          //
    {"flat-job-archive", no_argument, nullptr, 'f'},     //
    {"no-date", no_argument, nullptr, 'D'},              //
    {"date", no_argument, nullptr, 'U'},                 //
    {"system", no_argument, nullptr, 's'},               //
    {"system-recursive", no_argument, nullptr, 'S'},     //
    {"kernel", no_argument, nullptr, 'k'},               //
    {"kernel-recursive", no_argument, nullptr, 'K'},     //
    {"root-job", no_argument, nullptr, 'a'},             //
    {nullptr, no_argument, nullptr, 0},                  //
};

}  // namespace

int main(int argc, char** argv) {
  Flags flags;
  bool allow_jobs = false;
  auto handle_job = WriteManyCoreFiles;
  bool dump_root_job = false;

  auto usage = [&](int status = EXIT_FAILURE) {
    std::cerr << "Usage: " << argv[0] << R"""( [SWITCHES...] PID...

    --help, -h                         print this message
    --output-prefix=PREFIX, -o PREFIX  write <PREFIX><PID>, not core.<PID>
    --zstd, -z                         compress output files with zstd -11
    --limit=BYTES, -l BYTES            truncate output to BYTES per process
    --exclude-memory, -M               exclude all process memory from dumps
    --no-threads, -t                   collect only memory, threads left to run
    --jobs, -J                         allow PIDs to be job KOIDs instead
    --job-archive, -j                  write job archives, not process dumps
    --flat-job-archive, -f             write flattened job archives
    --no-children, -c                  don't recurse to child jobs
    --no-processes, -p                 don't dump processes found in jobs
    --no-date, -D                      don't record dates in dumps
    --date, -U                         record dates in dumps (default)
    --system, -s                       include system-wide information
    --system-recursive, -S             ... repeated in each child dump
    --kernel, -k                       include privileged kernel information
    --kernel-recursive, -K             ... repeated in each child dump
    --root-job, -a                     dump the root job

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

With --root-job (-a), dump the root job.  Without --no-children, that means
dumping every job on the system; and without --no-process, it means dumping
every process on the system.  Doing this without --no-threads may deadlock
essential services.  PID arguments are not allowed with --root-job unless
--no-children is also given, since they would always be redundant.
)""";
    return status;
  };

  while (true) {
    switch (getopt_long(argc, argv, kOptString, kLongOpts, nullptr)) {
      case -1:
        // This ends the loop.  All other cases continue (or return).
        break;

      case 'D':
        flags.record_date = false;
        continue;

      case 'U':
        flags.record_date = true;
        continue;

      case 'o':
        flags.output_prefix = optarg;
        continue;

      case 'l': {
        char* p;
        flags.limit = strtoul(optarg, &p, 0);
        if (*p != '\0') {
          return usage();
        }
        continue;
      }

      case 'm':
        flags.dump_memory = false;
        continue;

      case 't':
        flags.collect_threads = false;
        continue;

      case 'f':
        flags.flatten_jobs = true;
        [[fallthrough]];

      case 'j':
        handle_job = WriteDump<JobDumper>;
        [[fallthrough]];

      case 'J':
        allow_jobs = true;
        continue;

      case 'c':
        flags.collect_job_children = false;
        continue;

      case 'p':
        flags.collect_job_processes = false;
        continue;

      case 'S':
        flags.repeat_system = true;
        [[fallthrough]];

      case 's':
        flags.collect_system = true;
        continue;

      case 'K':
        flags.repeat_kernel = true;
        [[fallthrough]];

      case 'k':
        flags.collect_kernel = true;
        continue;

      case 'a':
        dump_root_job = true;
        continue;

      case 'z':
        flags.zstd = true;
        continue;

      default:
        return usage(EXIT_SUCCESS);
    }
    break;
  }

  if (optind == argc && !dump_root_job) {
    return usage();
  }

  if (optind != argc && dump_root_job && flags.collect_job_children) {
    std::cerr << "PID arguments are redundant with root job" << std::endl;
  }

  int exit_status = EXIT_SUCCESS;

  zxdump::TaskHolder holder;
  if (auto root = zxdump::GetRootJob(); root.is_error()) {
    std::cerr << "cannot get root job: " << root.error_value() << std::endl;
    exit_status = EXIT_FAILURE;
  } else {
    if (dump_root_job) {
      zx_info_handle_basic_t info;
      zx_status_t status = root.value().get_info(  //
          ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr);
      if (status != ZX_OK) {
        std::cerr << zxdump::Error{"zx_object_get_info", status} << std::endl;
        status = EXIT_FAILURE;
      } else {
        zx::handle job;
        zx_status_t status = root.value().duplicate(ZX_RIGHT_SAME_RIGHTS, &job);
        if (status != ZX_OK) {
          std::cerr << zxdump::Error{"zx_handle_duplicate", status} << std::endl;
          exit_status = EXIT_FAILURE;
        } else if (!handle_job(JobDumper{zx::job{std::move(job)}, info.koid}, flags))
          exit_status = EXIT_FAILURE;
      }
    }

    auto result = holder.Insert(std::move(root).value());
    if (result.is_error()) {
      std::cerr << "root job: " << root.error_value() << std::endl;
      exit_status = EXIT_FAILURE;
    }
  }

  for (int i = optind; i < argc; ++i) {
    char* p;
    zx_koid_t pid = strtoul(argv[i], &p, 0);
    if (*p != '\0') {
      std::cerr << "Not a PID or job KOID: " << argv[i] << std::endl;
      return usage();
    }

    auto result = holder.root_job().find(pid);
    if (result.is_error()) {
      std::cerr << pid << ": " << result.error_value() << std::endl;
      exit_status = EXIT_FAILURE;
      continue;
    }

    zxdump::Task& task = result.value();
    switch (task.type()) {
      case ZX_OBJ_TYPE_PROCESS:
        if (!WriteDump(ProcessDumper{zx::process{task.Reap()}, pid}, flags)) {
          exit_status = EXIT_FAILURE;
        }
        break;

      case ZX_OBJ_TYPE_JOB:
        if (allow_jobs) {
          if (!handle_job(JobDumper{zx::job{task.Reap()}, pid}, flags)) {
            exit_status = EXIT_FAILURE;
          }
          break;
        }
        [[fallthrough]];

      default:
        std::cerr << pid << ": KOID is not a process\n";
        exit_status = EXIT_FAILURE;
        break;
    }
  }

  return exit_status;
}
