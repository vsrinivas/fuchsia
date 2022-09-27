// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_DUMP_H_
#define SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_DUMP_H_

#include <lib/fit/function.h>
#include <lib/fitx/result.h>
#include <lib/zx/job.h>
#include <lib/zx/process.h>

#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>

#include "types.h"

namespace zxdump {

// This is the default limit on ET_CORE file size (in bytes), i.e. unlimited.
inline constexpr size_t DefaultLimit() { return std::numeric_limits<size_t>::max(); }

// This is used in the return value of the `prune_segment` callback passed to
// zxdump::ProcessDump<...>::CollectProcess.  It says how much of the segment
// to include in the dump.  Default-constructed state elides the whole segment.
//
// The callback receives zx_info_maps_t and zx_info_vmo_t data about the
// mapping and the memory to consider; and an zxdump::SegmentDisposition
// describing the default policy, which is usually to dump the whole thing,
// i.e. `filesz = maps.size`.  It can set `filesz = 0` to elide the segment;
// or set it to a smaller size to include only part of the segment.
//
// TODO(mcgrathr): Add PT_NOTE feature later.
struct SegmentDisposition {
  // The leading subset of the segment that should be included in the dump.
  // This can be zero to elide the whole segment, and must not be greater
  // than the original p_filesz value.  This doesn't have to be page-aligned,
  // but the next segment will be written at a page-aligned offset and the
  // gap filled with zero bytes (or a sparse region of the file) so there's
  // not much point in eliding a partial page.
  size_t filesz = 0;
};

using SegmentCallback = fit::function<fitx::result<Error, SegmentDisposition>(
    const SegmentDisposition&, const zx_info_maps_t&, const zx_info_vmo_t&)>;

// zxdump::ProcessDump<zx::process> or zxdump::ProcessDump<zx::unowned_process>
// represents one dump being made from the process whose handle is transferred
// or borrowed in the constructor argument.  The same object can be reset and
// used again to make another dump from the same process, but most often this
// object is only kept alive while one dump is being collected and written out.
// These are move-only, move-assignable, and default-constructible types.
//
// The ProcessDumpBase class defines the public API for dumping even though the
// class isn't used directly.  The ProcessDump template class below adapts to
// using this API with either an owned or an unowned process handle.
//
// The methods to produce the dump output work with any callable object that
// accepts a monotonically-increasing size_t offset in the "dump file" (really,
// stream position) and a zxdump::ByteView chunk of output.  That call should
// return some `fitx::result<error_type>` type.  The methods here propagate any
// error result by returning `fitx::result<error_type, ...>` values.
// zxdump::FdWriter (<lib/zxdump/fd-writer.h>) and similar objects return
// callable objects meant to be passed in here.

class DumpBase {
 protected:
  using DumpCallback = fit::function<bool(size_t offset, ByteView data)>;

  // This does some type erasure for a dump callback function and its result
  // type.  It holds the callable and a result object used as an indirect
  // result parameter for the wrapped callable.  The type-erased callback
  // returns true if there is an error, so iteration bails out early and lets
  // the caller (DumpHeaders or DumpMemory, below) propagate the result object.
  template <typename Dump>
  class DumpWrapper {
   public:
    using DumpResult = std::decay_t<std::invoke_result_t<Dump, size_t, ByteView>>;

    explicit DumpWrapper(Dump dump) : dump_(std::move(dump)) {}

    DumpCallback callback() {
      return [this](size_t offset, ByteView data) -> bool {
        dump_result_ = dump_(offset, data);
        return dump_result_.is_error();
      };
    }

    DumpResult& operator*() { return dump_result_; }

    DumpResult* operator->() { return &dump_result_; }

    template <typename T>
    fitx::result<DumpError<Dump>, T> error_or(std::string_view op,
                                              fitx::result<Error, T> op_result) {
      if (dump_result_.is_error()) {
        DumpError<Dump> error(Error{.op_ = op, .status_ = ZX_OK});
        error.dump_error_ = std::move(dump_result_).error_value();
        return fitx::error{std::move(error)};
      }
      if (op_result.is_error()) {
        return fitx::error{DumpError<Dump>{std::move(op_result).error_value()}};
      }
      return fitx::ok(std::move(op_result).value());
    }

   private:
    Dump dump_;
    DumpResult dump_result_ = fitx::ok();
  };

  // Deduction guide.
  template <typename Dump>
  DumpWrapper(Dump&&) -> DumpWrapper<std::decay_t<Dump>>;
};

// This defines the the public API methods for dumping (see above).
class ProcessDumpBase : protected DumpBase {
 public:
  ProcessDumpBase(ProcessDumpBase&&) noexcept;
  ProcessDumpBase& operator=(ProcessDumpBase&&) noexcept;

  ~ProcessDumpBase() noexcept;

  // Reset to initial state, except that if the process is already suspended,
  // it stays that way.
  void clear();

  // If this is called before DumpHeaders, the dump will include a date note.
  void set_date(time_t date);

  // This can be called at most once and must be called first if at all.  If
  // this is not called, then threads may be allowed to run while the dump
  // takes place, yielding an inconsistent memory image; and CollectProcess
  // will report only about memory and process-wide state, nothing about
  // threads.  Afterwards the process remains suspended until the ProcessDump
  // object is destroyed.
  fitx::result<Error> SuspendAndCollectThreads();

  // Collect system-wide information.  This is always optional, but it must
  // always be called before CollectProcess, if called at all.  The system
  // information is included in the total size returned by CollectProcess.
  fitx::result<Error> CollectSystem();

  // This can be called first or after SuspendAndCollectThreads.
  //
  // This collects information about memory and other process-wide state.  The
  // return value gives the total size of the ET_CORE file to be written.
  // Collection is cut short without error if the ET_CORE file would already
  // exceed the size limit without even including the memory.  See above for
  // how the callback is used.
  //
  // When this is complete, all data has been collected and all ET_CORE layout
  // has been done and the live data from the process won't be consulted again.
  // The only state still left to be collected from the process is the contents
  // of its memory.
  fitx::result<Error, size_t> CollectProcess(SegmentCallback prune_segment,
                                             size_t limit = DefaultLimit());

  // This must be called after CollectProcess.
  //
  // Accumulate header and note data to be written out, by repeatedly calling
  // `dump(size_t offset, ByteView data)`.  The Dump callback returns some
  // `fitx::result<error_type>` type.  DumpHeaders returns a result of type
  // `fitx::result<zxdump::DumpError<Dump>, size_t>` with the result of the
  // first failing callback, or with the total number of bytes dumped (i.e. the
  // ending value of `offset`).
  //
  // This can be used to collect data in place or to stream it out.  The
  // callbacks supply a stream of data where the first chunk has offset 0 and
  // later chunks always increase the offset.  This streams out the ELF file
  // and program headers, and then the note data that collects all the
  // process-wide, and (optionally) thread, state.  The views point into
  // storage helds inside the DumpProcess object.  They can be used freely
  // until the object is destroyed or clear()'d.
  template <typename Dump>
  fitx::result<DumpError<Dump>, size_t> DumpHeaders(Dump&& dump, size_t limit = DefaultLimit()) {
    using namespace std::literals;
    DumpWrapper wrapper(std::forward<Dump>(dump));
    return wrapper.error_or("DumpHeader"sv, DumpHeadersImpl(wrapper.callback(), limit));
  }

  // This must be called after DumpHeaders.
  //
  // Stream out memory data for the PT_LOAD segments, by repeatedly calling
  // `dump(size_t offset, ByteView data)` as in DumpHeaders, above.  While
  // DumpHeaders can really only fail if the Dump function returns an error,
  // DumpMemory's error result might have `.dump_error_ == std::nullopt` when
  // there was an error for reading memory from the process.  On success,
  // result value is the total byte size of the ET_CORE file, which is now
  // complete.  (This includes the size returned by DumpHeaders plus all the
  // memory segments written and any padding in between.)
  //
  // The offset in the first callback is greater than the offset in the last
  // DumpHeaders callback, and later callbacks always increase the offset.
  // There may be a gap from the end of previous chunk, which should be filled
  // with zero (or made sparse in the output file).  Unlike DumpHeaders, the
  // view passed to the `dump` callback here points into a temporary buffer
  // that will be reused for the next callback.  So this `dump` callback must
  // stream the data out or copy it, not just accumulate the view objects.
  template <typename Dump>
  fitx::result<DumpError<Dump>, size_t> DumpMemory(Dump&& dump, size_t limit = DefaultLimit()) {
    using namespace std::literals;
    DumpWrapper wrapper(std::forward<Dump>(dump));
    return wrapper.error_or("DumpMemory"sv, DumpMemoryImpl(wrapper.callback(), limit));
  }

 protected:
  class Collector;

  using DumpCallback = fit::function<bool(size_t offset, ByteView data)>;

  ProcessDumpBase() = default;

  void Emplace(zx::unowned_process process);

 private:
  fitx::result<Error, size_t> DumpHeadersImpl(DumpCallback dump, size_t limit);
  fitx::result<Error, size_t> DumpMemoryImpl(DumpCallback dump, size_t limit);

  std::unique_ptr<Collector> collector_;
};

template <typename Handle>
class ProcessDump : public ProcessDumpBase {
 public:
  static_assert(std::is_same_v<Handle, zx::process> || std::is_same_v<Handle, zx::unowned_process>);

  ProcessDump() = default;

  // This takes ownership of the handle in the zx::process instantiation.
  explicit ProcessDump(Handle process) noexcept;

 private:
  Handle process_;
};

static_assert(std::is_default_constructible_v<ProcessDump<zx::process>>);
static_assert(std::is_move_constructible_v<ProcessDump<zx::process>>);
static_assert(std::is_move_assignable_v<ProcessDump<zx::process>>);

static_assert(std::is_default_constructible_v<ProcessDump<zx::unowned_process>>);
static_assert(std::is_move_constructible_v<ProcessDump<zx::unowned_process>>);
static_assert(std::is_move_assignable_v<ProcessDump<zx::unowned_process>>);

// zxdump::JobDump<zx::job> or zxdump::JobDump<zx::unowned_job> represents one
// dump being made from the job whose handle is transferred or borrowed in the
// constructor argument.  The same object can be reset and used again to make
// another dump from the same job, but most often this object is only kept
// alive while one dump is being collected and written out.  These are
// move-only, move-assignable, and default-constructible types.
//
// The JobDumpBase class defines the public API for dumping even though the
// class isn't used directly.  The JobDump template class below adapts to using
// this API with either an owned or an unowned job handle.
//
// A job is dumped into a "job archive".  This contains information about the
// job itself, and can also contain multiple process dumps in `ET_CORE` files
// as members of the archive.  A job archive is streamed out via callbacks like
// process dumps are.  If process dumps are included, each is streamed out via
// its own zxdump::ProcessDump object in turn.
class JobDumpBase : protected DumpBase {
 public:
  using JobVector = std::vector<std::pair<zx::job, zx_koid_t>>;
  using ProcessVector = std::vector<std::pair<zx::process, zx_koid_t>>;

  JobDumpBase(JobDumpBase&&) noexcept;
  JobDumpBase& operator=(JobDumpBase&&) noexcept;

  ~JobDumpBase() noexcept;

  // Collect system-wide information.  This is always optional, but it must
  // always be called first, before CollectJob, if called at all.  The system
  // information is included in the total size returned by CollectJob.
  fitx::result<Error> CollectSystem();

  // Collect information about the job itself.  The result contains the size of
  // the job archive to dump just that information.  Note that this collection
  // is completely asynchronous with respect to the job and any process within
  // it.  The dump will be conducted on the basis of this data, but even as new
  // processes or child jobs come and go, they will not be collected.  (There
  // is no analog to zx::ProcessDump<...>::SuspendAndCollectThreads for jobs.)
  fitx::result<Error, size_t> CollectJob();

  // This must be called after CollectJob and before other dumping calls.  It
  // dumps the job archive header and the information CollectJob found.  This
  // alone results in a valid "stub" job archive that gives some summary data
  // about the job but doesn't include any process or child dumps.  On success,
  // its result's size_t value() is the total size written so far, which by
  // itself is already a valid archive file image for the "stub" job archive.
  template <typename Dump>
  fitx::result<DumpError<Dump>, size_t> DumpHeaders(Dump&& dump, time_t mtime = 0) {
    using namespace std::literals;
    DumpWrapper wrapper{std::forward<Dump>(dump)};
    return wrapper.error_or("DumpHeaders"sv, DumpHeadersImpl(wrapper.callback(), mtime));
  }

  // This begins a new file of the archive by streaming out its archive member
  // file header, which has a small fixed size.  The format requires that this
  // come after everything DumpHeaders writes.  The size of the member file
  // must already be known, though its contents can be streamed out piecemeal
  // after this.  After the headers written by DumpHeaders, a job archive
  // consists of any number of member files.  Each is either an ELF `ET_CORE`
  // file representing a process in this job; or another whole job archive
  // representing a child job.  The name of each file chosen by the dump-writer
  // need not be meaningful and is truncated to a short limit (16).  Each file
  // is understood based on its own format and contents, though names friendly
  // to human readers of `ar tv` output are recommended.
  template <typename Dump>
  static fitx::result<DumpError<Dump>, size_t> DumpMemberHeader(Dump&& dump, size_t offset,
                                                                std::string_view name, size_t size,
                                                                time_t mtime = 0) {
    using namespace std::literals;
    DumpWrapper wrapper{std::forward<Dump>(dump)};
    return wrapper.error_or("DumpMemberHeader"sv,
                            DumpMemberHeaderImpl(wrapper.callback(), offset, name, size, mtime));
  }

  // Return the size that DumpMemberHeader will always consume.
  [[gnu::const]] static size_t MemberHeaderSize();

  // This can be used either before or after using DumpHeaders or other calls.
  // This acquires job handles for all the child jobs CollectJob found; if
  // CollectJob wasn't called, then this does the necessary portion of its
  // work.  (If DumpHeaders is called after CollectChildren but before
  // CollectJob, then the job archive will not include all the normal job
  // information, but only the job information that lists the child KOIDs.)
  // Any job KOIDs that cannot be found are simply ignored as races with old
  // jobs being cleaned up.
  //
  // The caller can then create a JobDump object for each job and stream it out
  // after calling DumpMemberHeader.
  fitx::result<Error, JobVector> CollectChildren();

  // This can be used either before or after using DumpHeaders or other calls.
  // This acquires process handles for all the direct-child processes
  // CollectJob found; if CollectJob wasn't called, then this does the
  // necessary portion of its work.  (If DumpHeaders is called after
  // CollectProcesses but before CollectJob, then the job archive will not
  // include all the normal job information, but only the job information that
  // lists the process KOIDs.)  Any process KOIDs that cannot be found are
  // simply ignored as races with old processes dying.
  //
  // The caller can then create a ProcessDump object for each process and
  // stream it out after calling DumpMemberHeader.
  fitx::result<Error, ProcessVector> CollectProcesses();

 protected:
  class Collector;

  JobDumpBase() = default;

  void Emplace(zx::unowned_job job);

 private:
  fitx::result<Error, size_t> DumpHeadersImpl(DumpCallback dump, time_t mtime);
  static fitx::result<Error, size_t> DumpMemberHeaderImpl(DumpCallback dump, size_t offset,
                                                          std::string_view name, size_t size,
                                                          time_t mtime);

  std::unique_ptr<Collector> collector_;
};

template <typename Handle>
class JobDump : public JobDumpBase {
 public:
  static_assert(std::is_same_v<Handle, zx::job> || std::is_same_v<Handle, zx::unowned_job>);

  JobDump() = default;

  // This takes ownership of the handle in the zx::job instantiation.
  explicit JobDump(Handle job) noexcept;

 private:
  Handle job_;
};

static_assert(std::is_default_constructible_v<JobDump<zx::job>>);
static_assert(std::is_move_constructible_v<JobDump<zx::job>>);
static_assert(std::is_move_assignable_v<JobDump<zx::job>>);

static_assert(std::is_default_constructible_v<JobDump<zx::unowned_job>>);
static_assert(std::is_move_constructible_v<JobDump<zx::unowned_job>>);
static_assert(std::is_move_assignable_v<JobDump<zx::unowned_job>>);

}  // namespace zxdump

#endif  // SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_DUMP_H_
