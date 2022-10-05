// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_DECOMPRESS_H_
#define SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_DECOMPRESS_H_

#include <lib/fit/result.h>
#include <lib/stdcompat/span.h>

#include <memory>
#include <string_view>

#include "storage-traits.h"

namespace zbitl::decompress {

// This is the default argument for the zbitl::View::CopyStorageItem callback
// to allocate scratch memory.  It uses normal `new std::byte[]`.  If explicit
// callbacks are provided instead, the library won't need to link in the
// standard C++ library allocator used by this.
fit::result<std::string_view, std::unique_ptr<std::byte[]>> DefaultAllocator(size_t bytes);

class OneShot {
 public:
  // This is public only for test use.
  static size_t GetScratchSize();

  // Called (once) with the whole payload and returns success only if exactly
  // the whole output buffer was filled.
  template <typename Allocator>
  static fit::result<std::string_view> Decompress(cpp20::span<std::byte> out, ByteView payload,
                                                  Allocator&& allocator) {
    const size_t need = GetScratchSize();
    auto scratch = allocator(need);
    if (scratch.is_error()) {
      return scratch.take_error();
    }
    Context* dctx = Init(scratch.value().get(), need);
    return DecompressImpl(dctx, out, payload);
  }

 private:
  struct Context;  // Opaque.

  // Set up the decompression context in a buffer according to GetScratchSize.
  static Context* Init(void* scratch_space, size_t scratch_size);

  static fit::result<std::string_view> DecompressImpl(Context* dctx, cpp20::span<std::byte> out,
                                                      ByteView in);
};

class Streaming {
 public:
  template <bool Buffered, typename Allocator>
  static auto Create(ByteView probe, Allocator&& allocator) {
    if constexpr (Buffered) {
      // Returns fit::result<std::string_view, lambda>.  On success, lambda is
      // (ByteView& in) -> fit::result<std::string_view, cpp20::span<std::byte>>
      // It updates the `in` argument for the data consumed, and it returns the
      // a buffer of decompressed data that can be used until the next call.
      return Streaming::CreateBufferedImpl(probe, std::forward<Allocator>(allocator));
    } else {
      // Returns fit::result<std::string_view, lambda>.  On success, the
      // lambda is (cpp20::span<std::byte> out, ByteView& in) ->
      // fit::result<std::string_view, cpp20::span<std::byte>>.  It updates the
      // `in` argument for the data consumed, and it returns the remainder of
      // the `out` argument not yet written.
      return Streaming::CreateUnbufferedImpl(probe, std::forward<Allocator>(allocator));
    }
  }

 private:
  struct Context;  // Opaque.

  struct ScratchSize {
    size_t scratch_size;
    size_t buffer_size;
  };

  // Calculate the scratch space required to decompress the payload, given an
  // initial chunk of the payload of at least zbitl::kReadMinimum bytes.
  static fit::result<std::string_view, ScratchSize> GetScratchSize(ByteView probe);

  // Set up the decompression context.
  static Context* Init(void* scratch_space, size_t scratch_size);

  // Decompress a chunk of payload into the buffer.  The returned buffer is a
  // subset of the supplied buffer.  The argument is updated to leave only the
  // unprocessed remainder.
  static fit::result<std::string_view, cpp20::span<std::byte>> Decompress(
      Context* dctx, cpp20::span<std::byte> buffer, ByteView& chunk);

  // This creates the decompressor object that View::DecompressStorage calls
  // repeatedly.  The object is a lambda that holds onto the owning objects
  // returned by the allocator.
  static constexpr auto MakeBuffered = [](ScratchSize need, auto&& owner, auto&& buffer) {
    Context* dctx = Init(owner.get(), need.scratch_size);
    cpp20::span<std::byte> out{
        reinterpret_cast<std::byte*>(buffer.get()),
        need.buffer_size,
    };
    return [owner = std::forward<decltype(owner)>(owner),
            buffer = std::forward<decltype(buffer)>(buffer), dctx,
            out](ByteView& in) -> fit::result<std::string_view, cpp20::span<std::byte>> {
      auto result = Decompress(dctx, out, in);
      if (result.is_error()) {
        return result.take_error();
      }
      return fit::ok(out.subspan(0, out.size() - result.value().size()));
    };
  };

  template <typename Allocator>
  static auto CreateBufferedImpl(ByteView probe, Allocator&& allocator)
      -> fit::result<std::string_view,  // A lambda type
                     decltype(MakeBuffered({}, allocator({}).value(), allocator({}).value()))> {
    ScratchSize need;
    if (auto result = GetScratchSize(probe); result.is_error()) {
      return result.take_error();
    } else {
      need = result.value();
    }
    auto scratch = allocator(need.scratch_size);
    if (scratch.is_error()) {
      return scratch.take_error();
    }
    auto buffer = allocator(need.buffer_size);
    if (buffer.is_error()) {
      return buffer.take_error();
    }
    return fit::ok(MakeBuffered(need, std::move(scratch).value(), std::move(buffer).value()));
  }

  // This creates the decompressor object that View::DecompressStorage calls
  // repeatedly.  The object is a lambda that holds onto the owning object
  // returned by the allocator.
  static constexpr auto MakeUnbuffered = [](size_t scratch_size, auto&& owner) {
    Context* dctx = Init(owner.get(), scratch_size);
    return [owner = std::forward<decltype(owner)>(owner), dctx](
               cpp20::span<std::byte> out, ByteView& in) { return Decompress(dctx, out, in); };
  };

  template <typename Allocator>
  static auto CreateUnbufferedImpl(ByteView probe, Allocator&& allocator)
      -> fit::result<std::string_view, decltype(MakeUnbuffered({}, allocator({}).value()))> {
    ScratchSize need;
    if (auto result = GetScratchSize(probe); result.is_error()) {
      return result.take_error();
    } else {
      need = result.value();
    }
    auto scratch = allocator(need.scratch_size);
    if (scratch.is_error()) {
      return scratch.take_error();
    }
    return fit::ok(MakeUnbuffered(need.scratch_size, std::move(scratch).value()));
  }
};

}  // namespace zbitl::decompress

#endif  // SRC_LIB_ZBITL_INCLUDE_LIB_ZBITL_DECOMPRESS_H_
