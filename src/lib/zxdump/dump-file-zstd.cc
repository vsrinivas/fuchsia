// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dump-file-zstd.h"

namespace zxdump::internal {

bool DumpFile::IsCompressed(ByteView header) { return ZSTD_isFrame(header.data(), header.size()); }

fitx::result<Error, std::unique_ptr<DumpFile>> DumpFile::Decompress(FileRange where,
                                                                    ByteView header) {
  auto decompressor = std::make_unique<Zstd>(*this, where);
  auto result = decompressor->Pump(header, 0);
  if (result.is_error()) {
    return result.take_error();
  }
  return fitx::ok(std::move(decompressor));
}

// This really just has to return nonzero.
// We don't know the size of the uncompressed file.
size_t DumpFile::Zstd::size() const {
  return file_pos_.size == 0 ? buffer_.size() : file_pos_.size;
}

fitx::result<Error, ByteView> DumpFile::Zstd::ReadProbe(FileRange where) {
  return Read(where, false, true);
}

fitx::result<Error, ByteView> DumpFile::Zstd::ReadEphemeral(FileRange where) {
  return Read(where, false, false);
}

fitx::result<Error, ByteView> DumpFile::Zstd::ReadPermanent(FileRange where) {
  return Read(where, true, false);
}

void DumpFile::Zstd::shrink_to_fit() {
  file_->shrink_to_fit();
  ctx_.reset();
}

// Put some data through the decompressor.
fitx::result<Error, bool> DumpFile::Zstd::Pump(ByteView compressed, size_t skip) {
  if (buffer_.empty()) {
    buffer_ = Buffer{ZSTD_DStreamOutSize()};
  }
  ZSTD_inBuffer in = {compressed.data(), compressed.size(), 0};
  ZSTD_outBuffer out = {buffer_.data(), buffer_.size(), skip};
  size_t result = ZSTD_decompressStream(ctx_.get(), &out, &in);
  if (ZSTD_isError(result)) {
    return fitx::error(Error{
        ZSTD_getErrorName(result),
        ZX_ERR_IO_DATA_INTEGRITY,
    });
  }

  // We've advanced in the uncompressed file image past the old buffer.
  // The new buffer now represents the next chunk just decompressed.
  buffer_range_.offset += buffer_range_.size - skip;
  buffer_range_.size = out.pos;

  // We've advanced in the compressed file image only however much the
  // decompressor consumed.
  file_pos_.offset += in.pos;

  // Store the decompressor's hint about how much to read next time.
  // This is zero when the stream is complete.
  file_pos_.size = result;
  return fitx::ok(in.pos > 0);
}

fitx::result<Error, ByteView> DumpFile::Zstd::Read(FileRange where, bool permanent, bool probe) {
  if (where.offset < buffer_range_.offset) {
    return fitx::error(Error{
        "random access not available",
        ZX_ERR_IO_REFUSED,
    });
  }

  if (!permanent) {
    // Any buffers saved just for dangling ephemeral results are dead now.
    ephemeral_.clear();
  }

  ByteView buffered{buffer_.data(), buffer_range_.size};

  // For a permanent read, make the buffer size exact so as to
  // transfer the whole buffer later.  Otherwise, always make it at
  // least big enough for the recommended decompressor chunk size.
  size_t min_size = permanent ? 0 : ZSTD_DStreamOutSize();
  min_size = std::max(static_cast<size_t>(where.size), min_size);
  auto replace_buffer = [save_old_buffer = permanent, min_size, &buffered,
                         this](bool stuck = false) mutable {
    size_t new_size =
        stuck ? std::max(buffered.size() + ZSTD_DStreamOutSize(), min_size) : min_size;

    // Ordinarily we only need to keep the buffer alive long enough
    // to copy old data out of it.
    Buffer keepalive;
    if (buffer_.size() != 0) {
      std::swap(buffer_, keepalive);

      if (save_old_buffer) {
        // The first time the buffer needs to be reused or resized, then
        // the last ephemeral use may still have dangling pointers into the
        // old buffer so it must be saved until the next ephemeral call.
        // When it's not saved, the old buffer is returned here to be kept
        // alive long enough to copy data out of it into its replacement.
        ephemeral_.push_front(std::move(keepalive));
        save_old_buffer = false;
      }
    }

    if (buffer_.size() < new_size) {
      buffer_ = Buffer{new_size};
      if (!buffered.empty()) {
        buffered.copy(buffer_.data(), buffer_.size());
      }
    } else {
      // The old buffer is actually big enough already.
      // Just move the existing data around.
      buffer_ = std::move(keepalive);
      if (!buffered.empty()) {
        memmove(buffer_.data(), buffered.data(), buffered.size());
      }
    }
    buffered = {buffer_.data(), buffered.size()};
  };

  auto ok = [&]() -> fitx::result<Error, ByteView> {
    if (!probe && buffered.size() < where.size) {
      return TruncatedDump();
    }
    if (permanent) {
      Buffer saved;
      if (buffered.data() == buffer_.data() && buffer_.size() == where.size) {
        // The whole buffer is just right, so steal it to be permanent.
        std::swap(saved, buffer_);
      } else {
        // Copy into a new permanent buffer.
        saved = Buffer{where.size};
        buffered = {saved.data(), buffered.copy(saved.data(), saved.size())};
      }
      keepalive_.push_front(std::move(saved));
    }
    return fitx::ok(buffered);
  };

  if (size_t ofs = where.offset - buffer_range_.offset; ofs < buffered.size()) {
    // Some of the data we need is in the buffer we already have.
    buffered = buffered.substr(ofs, where.size);
    if (buffered.size() == where.size) {
      return ok();
    }

    // We've already buffered some data we need, but we need more data
    // that's contiguous with that tail.  So move the tail we need into
    // the head of the buffer so we can fill the rest.
    replace_buffer();
  } else {
    buffered = {};
  }

  // The buffer now represents what we have of the exact range we need,
  // even if that's nothing.
  buffer_range_.offset = where.offset;
  buffer_range_.size = buffered.size();

  // Decompress more data as long as we don't have enough data in the
  // buffer yet and the compressed stream hasn't ended (as indicated by
  // file_pos_.size == 0).
  while (buffered.size() < where.size && file_pos_.size > 0) {
    if (buffer_.size() < where.size) {
      // The current buffer is too small for this request.  Get a new one.
      replace_buffer();
    }

    // Read some more data.  The decompressor said last time how much more.
    auto read_result = file_->ReadEphemeral(file_pos_);
    if (read_result.is_error()) {
      return read_result.take_error();
    }

    // Put that data through the decompressor.
    auto result = Pump(read_result.value(), buffered.size());
    buffered = {buffer_.data(), buffer_range_.size};

    if (result.is_error()) {
      return read_result.take_error();
    }

    if (!result.value()) {
      // The decompressor was not able to make progress because the output
      // buffer is full.  Make it larger.
      replace_buffer(true);
    }
  }

  ZX_DEBUG_ASSERT(buffer_range_.offset == where.offset);
  buffered = buffered.substr(0, where.size);
  return ok();
}

}  // namespace zxdump::internal
