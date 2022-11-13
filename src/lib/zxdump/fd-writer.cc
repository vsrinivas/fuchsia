// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxdump/fd-writer.h>
#include <sys/uio.h>
#include <unistd.h>
#include <zircon/assert.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <utility>

namespace zxdump {

using namespace std::literals;

namespace {

auto ErrnoError(std::string_view op) { return fit::error{FdError{.op_ = op, .error_ = errno}}; }

}  // namespace

void FdWriter::Accumulate(size_t offset, ByteView data) {
  if (data.empty()) {
    return;
  }
  ZX_ASSERT_MSG(offset >= total_, "Accumulate %zu bytes at offset %zu vs total %zu", data.size(),
                offset, total_);
  ZX_ASSERT_MSG(offset - total_ == fragments_.size_bytes_,
                "Accumulate at %zu - %zu gap != %zu accumulation", offset, total_,
                fragments_.size_bytes_);
  fragments_.iov_.push_back({const_cast<std::byte*>(data.data()), data.size()});
  fragments_.size_bytes_ += data.size();
}

// Flush accumulated fragments.
fit::result<FdWriter::error_type, size_t> FdWriter::WriteFragments() {
  // Consume the old state.
  Fragments fragments = std::exchange(fragments_, {});

  // Drain the whole vector of fragments to write, making as few writev calls
  // as possible.
  size_t written = 0;
  for (size_t i = 0; i < fragments.iov_.size(); ++i) {
    ZX_DEBUG_ASSERT(written < fragments.size_bytes_);
    size_t count = std::min(size_t{IOV_MAX}, fragments.iov_.size() - i);
    ssize_t n = writev(fd_.get(), &fragments.iov_[i], static_cast<int>(count));
    if (n <= 0) {
      if (n == 0) {
        errno = EIO;
      }
      return ErrnoError("writev"sv);
    }
    size_t wrote = static_cast<size_t>(n);
    ZX_ASSERT(wrote <= fragments.size_bytes_ - written);
    written += wrote;

    // Trim off all the fragments that writev consumed.
    while (i < fragments.iov_.size() && fragments.iov_[i].iov_len <= wrote) {
      ZX_DEBUG_ASSERT(fragments.iov_[i].iov_len > 0);
      wrote -= fragments.iov_[i].iov_len;
      ++i;
    }

    // Adjust the next remaining fragment if writev wrote part of it.
    if (wrote > 0 && i < fragments.iov_.size()) {
      iovec& iov = fragments.iov_[i];
      ZX_DEBUG_ASSERT(iov.iov_len > wrote);
      iov.iov_len -= wrote;
      iov.iov_base = static_cast<std::byte*>(iov.iov_base) + wrote;
    }
  }

  total_ += written;
  ZX_ASSERT(written == fragments.size_bytes_);
  return fit::ok(written);
}

fit::result<FdWriter::error_type> FdWriter::Write(size_t offset, ByteView data) {
  ZX_ASSERT(offset >= total_);
  ZX_ASSERT(!data.empty());
  ZX_DEBUG_ASSERT(data.data());

  auto write_data = [this](ByteView data)  // Write the whole chunk.
      -> fit::result<FdWriter::error_type> {
    do {
      ssize_t n = write(fd_.get(), data.data(), data.size());
      if (n <= 0) {
        if (n == 0) {
          errno = EIO;
        }
        return ErrnoError("write");
      }
      const size_t wrote = static_cast<size_t>(n);
      ZX_ASSERT(wrote <= data.size());
      data.remove_prefix(wrote);
      total_ += wrote;
    } while (!data.empty());
    return fit::ok();
  };

  // Seek or fill past any gap.
  const size_t gap = offset - total_;

  if (gap > 0) {
    // Pad ahead to the aligned offset.  Seek ahead to leave holes
    // in a sparse file if the filesystem supports that.
    if (!is_pipe_ && lseek(fd_.get(), static_cast<off_t>(gap), SEEK_CUR) < 0) {
      is_pipe_ = errno == ESPIPE;
      if (!is_pipe_) {
        return ErrnoError("lseek");
      }
    }
    if (is_pipe_) {
      // It's not seekable, so fill in with zero bytes.
      auto zero = std::make_unique<const std::byte[]>(gap);
      auto result = write_data({zero.get(), gap});
      if (result.is_error()) {
        return result;
      }
    } else {
      // When we write below, we will have "written" the gap bytes too.
      total_ = offset;
    }
  }

  // Now write the actual data and total_ will again reflect the end of file.
  return write_data(data);
}

}  // namespace zxdump
