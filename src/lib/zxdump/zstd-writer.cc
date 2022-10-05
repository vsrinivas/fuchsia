// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxdump/zstd-writer.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <cerrno>
#include <thread>

#include <zstd/zstd.h>

namespace zxdump {

ZstdWriter::ZstdWriter(fbl::unique_fd fd)
    : ctx_(ZSTD_createCStream()),
      buffer_(new std::byte[ZSTD_CStreamOutSize()]),
      fd_(std::move(fd)) {
  auto set = [ctx = static_cast<ZSTD_CCtx*>(ctx_)](auto param, auto value) {
    ZSTD_CCtx_setParameter(ctx, param, value);
  };
  set(ZSTD_c_compressionLevel, 11);
  set(ZSTD_c_enableLongDistanceMatching, 1);
  set(ZSTD_c_nbWorkers, std::thread::hardware_concurrency());
}

ZstdWriter::~ZstdWriter() {
  auto ctx = static_cast<ZSTD_CCtx*>(ctx_);
  ZSTD_freeCStream(ctx);
}

fit::result<ZstdWriter::error_type> ZstdWriter::Flush() {
  ByteView out{buffer_.get(), buffer_pos_};
  buffer_pos_ = 0;
  while (!out.empty()) {
    ssize_t n = write(fd_.get(), out.data(), out.size());
    if (n < 0) {
      return fit::error{"write"};
    }
    if (n == 0) {
      errno = EAGAIN;
      return fit::error{"write returned zero"};
    }
    out.remove_prefix(n);
  }
  return fit::ok();
}

fit::result<ZstdWriter::error_type> ZstdWriter::Write(size_t offset, ByteView data) {
  ZX_ASSERT(offset >= offset_);
  ZX_ASSERT(!data.empty());
  ZX_DEBUG_ASSERT(data.data());

  // If there are holes we have to feed zero bytes to the compressor.
  while (offset > offset_) {
    static constexpr std::byte kZero[32] = {};
    auto pad = ByteView{kZero, sizeof(kZero)}.substr(0, offset - offset_);
    auto result = Write(offset_, pad);
    if (result.is_error()) {
      return result.take_error();
    }
  }

  ZSTD_inBuffer in = {data.data(), data.size(), 0};
  while (in.pos < in.size) {
    ZSTD_outBuffer out = {buffer_.get(), ZSTD_CStreamOutSize(), buffer_pos_};
    auto ctx = static_cast<ZSTD_CCtx*>(ctx_);
    size_t result = ZSTD_compressStream2(ctx, &out, &in, ZSTD_e_continue);
    buffer_pos_ = out.pos;
    if (ZSTD_isError(result)) {
      errno = 0;
      return fit::error{ZSTD_getErrorName(result)};
    }
    if (in.pos < in.size) {
      // Not all consumed yet, so flush the buffer.
      auto result = Flush();
      if (result.is_error()) {
        return result.take_error();
      }
    }
  }

  offset_ += in.pos;
  return fit::ok();
}

fit::result<ZstdWriter::error_type> ZstdWriter::Finish() {
  size_t compress_result;
  do {
    ZSTD_inBuffer in = {};
    ZSTD_outBuffer out = {buffer_.get(), ZSTD_CStreamOutSize(), buffer_pos_};
    auto ctx = static_cast<ZSTD_CCtx*>(ctx_);
    compress_result = ZSTD_compressStream2(ctx, &out, &in, ZSTD_e_end);
    buffer_pos_ = out.pos;
    if (ZSTD_isError(compress_result)) {
      errno = 0;
      return fit::error{ZSTD_getErrorName(compress_result)};
    }
    if (auto result = Flush(); result.is_error()) {
      return result.take_error();
    }
  } while (compress_result != 0);
  return fit::ok();
}

}  // namespace zxdump
