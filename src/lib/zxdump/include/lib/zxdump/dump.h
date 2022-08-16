// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_DUMP_H_
#define SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_DUMP_H_

#include <lib/fit/function.h>
#include <lib/fitx/result.h>
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

class ProcessDumpBase {
 public:
  ProcessDumpBase(ProcessDumpBase&&) = default;
  ProcessDumpBase& operator=(ProcessDumpBase&&) = default;

  ~ProcessDumpBase();

  // Reset to initial state, except that if the process is already suspended,
  // it stays that way.
  void clear();

  // This can be called at most once and must be called first if at all.  If
  // this is not called, then threads may be allowed to run while the dump
  // takes place, yielding an inconsistent memory image; and CollectProcess
  // will report only about memory and process-wide state, nothing about
  // threads.  Afterwards the process remains suspended until the ProcessDump
  // object is destroyed.
  fitx::result<Error> SuspendAndCollectThreads();

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
  auto DumpHeaders(Dump&& dump, size_t limit) {
    using namespace std::literals;
    return CallImpl(&ProcessDumpBase::DumpHeadersImpl, "DumpHeader"sv, std::forward<Dump>(dump),
                    limit);
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
  fitx::result<DumpError<Dump>, size_t> DumpMemory(Dump&& dump, size_t limit) {
    using namespace std::literals;
    return CallImpl(&ProcessDumpBase::DumpMemoryImpl, "DumpMemory"sv, std::forward<Dump>(dump),
                    limit);
  }

 protected:
  class Collector;

  using DumpCallback = fit::function<bool(size_t offset, ByteView data)>;

  ProcessDumpBase() = default;

  void Emplace(zx::unowned_process process);

 private:
  template <typename Impl, typename Dump>
  fitx::result<DumpError<Dump>, size_t> CallImpl(Impl&& impl, std::string_view op, Dump&& dump,
                                                 size_t limit) {
    fitx::result<DumpError<Dump>, size_t> result = fitx::ok(0);
    auto dump_callback = [&](size_t offset, ByteView data) {
      auto dump_result = dump(offset, data);
      if (dump_result.is_ok()) {
        return false;
      }
      DumpError<Dump> error(Error{.op_ = op, .status_ = ZX_OK});
      error.dump_error_ = std::move(dump_result).error_value(),
      result = fitx::error{std::move(error)};
      return true;
    };
    auto value = std::invoke(impl, this, dump_callback, limit);
    if (result.is_error()) {
      return std::move(result).take_error();
    }
    return fitx::ok(*value);
  }

  fitx::result<Error, size_t> DumpHeadersImpl(DumpCallback dump, size_t limit);

  fitx::result<Error, size_t> DumpMemoryImpl(DumpCallback callback, size_t limit);

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

}  // namespace zxdump

#endif  // SRC_LIB_ZXDUMP_INCLUDE_LIB_ZXDUMP_DUMP_H_
