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
#include <type_traits>

#include "types.h"

namespace zxdump {

// This is the default limit on ET_CORE file size (in bytes), i.e. unlimited.
inline constexpr size_t DefaultLimit() { return std::numeric_limits<size_t>::max(); }

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

  // This must be called first.
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
  fitx::result<Error, size_t> CollectProcess(size_t limit = DefaultLimit());

  // This must be called after CollectProcess.
  //
  // Accumulate header and note data to be written out, by repeatedly calling
  // `dump(size_t offset, ByteView data)`.  The callback returns some
  // `fitx::result<error_type>` type.  DumpHeaders returns a result of type
  // `fitx::result<error_type, size_t>` with the result of the first failing
  // callback, or with the total number of bytes dumped (i.e. the ending value
  // of `offset`).
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
    return CallImpl(&ProcessDumpBase::DumpHeadersImpl, std::forward<Dump>(dump), limit);
  }

 protected:
  class Collector;

  using DumpCallback = fit::function<bool(size_t offset, ByteView data)>;

  ProcessDumpBase() = default;

  void Emplace(zx::unowned_process process);

 private:
  template <typename Dump>
  using DumpResult = std::decay_t<decltype(std::declval<Dump>()(size_t{}, ByteView{}))>;

  template <typename Dump>
  using DumpError = std::decay_t<decltype(std::declval<DumpResult<Dump>>().error_value())>;

  template <typename Dump>
  using DumpMemoryResult = fitx::result<Error, fitx::result<DumpError<Dump>, size_t>>;

  template <typename Impl>
  using ImplResult = std::invoke_result_t<Impl, ProcessDumpBase*, DumpCallback, size_t>;

  template <typename Impl, typename Dump>
  auto CallImpl(Impl&& impl, Dump&& dump, size_t limit)
      -> fitx::result<DumpError<Dump>, ImplResult<Impl>> {
    using Result = std::decay_t<std::invoke_result_t<Dump, size_t, ByteView>>;
    Result result = fitx::ok();
    auto dump_callback = [&](size_t offset, ByteView data) {
      result = dump(offset, data);
      return result.is_error();
    };
    auto value = std::invoke(impl, this, dump_callback, limit);
    if (result.is_error()) {
      return std::move(result).take_error();
    }
    return fitx::ok(std::move(value));
  }

  size_t DumpHeadersImpl(DumpCallback dump, size_t limit);

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
