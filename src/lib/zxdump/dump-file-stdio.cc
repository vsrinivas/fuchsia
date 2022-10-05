// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dump-file-stdio.h"

namespace zxdump::internal {

DumpFile::Stdio::~Stdio() = default;

// Return the available subset of the requested data, a view valid for the
// life of the Stdio.
fit::result<Error, ByteView> DumpFile::Stdio::ReadPermanent(FileRange where) {
  auto result = Read(where);
  if (result.is_error()) {
    return result.take_error();
  }
  if (result->size() < where.size) {
    return TruncatedDump();
  }
  ByteView data{result->data(), result->size()};
  keepalive_.push_front(std::move(result).value());
  return fit::ok(data);
}

// Return the available subset of the requested data, a view valid only
// until the next call to this method.  The returned data might be less
// than what's requested if EOF is reached.
fit::result<Error, ByteView> DumpFile::Stdio::ReadProbe(FileRange where) {
  auto result = Read(where);
  if (result.is_error()) {
    return result.take_error();
  }
  ByteView data{result->data(), result->size()};
  ephemeral_buffer_ = std::move(result).value();
  ephemeral_buffer_range_ = where;
  return fit::ok(data);
}

// Return the available subset of the requested data, a view valid only
// until the next call to this method.  The data must be present.
fit::result<Error, ByteView> DumpFile::Stdio::ReadEphemeral(FileRange where) {
  auto result = ReadProbe(where);
  if (result.is_ok() && result->size() < where.size) {
    return TruncatedDump();
  }
  return result;
}

fit::result<Error, DumpFile::Buffer> DumpFile::Stdio::Read(FileRange where) {
  if (!stream_) {
    return fit::error(Error{"read_memory disabled", ZX_ERR_NOT_SUPPORTED});
  }

  // Seek if necessary and possible.
  if (where.offset != pos_ && !is_pipe_) {
    if (fseek(stream_.get(), static_cast<long int>(where.offset), SEEK_SET) == 0) {
      pos_ = where.offset;
    } else if (errno == ESPIPE) {
      is_pipe_ = true;
    } else {
      return fit::error(Error{"fseek", ZX_ERR_IO});
    }
  }

  // In general the reader can only ever need to look backward when attempting
  // random access for reading memory segments.  The one exception is after
  // reading the initial header probe with ReadEphemeral, when the next data
  // needed might overlap with the end of the probe that was more than it
  // turned out was actually needed for the header.  So in that case we can
  // steal the data from the ephemeral_buffer_ already on hand.
  Buffer buffer(where.size);
  std::byte* data = buffer.data();
  while (where.offset < pos_) {
    ByteView old_data{ephemeral_buffer_.data(), ephemeral_buffer_range_.size};
    if (where.offset >= ephemeral_buffer_range_.offset) {
      size_t ofs = where.offset - ephemeral_buffer_range_.offset;
      if (ofs < old_data.size()) {
        size_t copied = old_data.copy(data, where.size, ofs);
        data += copied;
        where.offset += copied;
        where.size -= copied;
        continue;
      }
    }
    return fit::error(Error{"random access not available", ZX_ERR_IO_REFUSED});
  }

  // Not seekable, so just eat any data being skipped over.
  while (where.offset > pos_) {
    if (getc(stream_.get()) == EOF) {
      return fit::error(Error{"getc", ZX_ERR_IO});
    }
    ++pos_;
  }

  while (where.size > 0) {
    size_t n = fread(data, 1, where.size, stream_.get());
    if (n == 0) {
      if (feof(stream_.get())) {
        break;
      }
      return fit::error(Error{"fread", ZX_ERR_IO});
    }
    pos_ += n;
    data += n;
    where.size -= n;
  }
  buffer.resize(data - buffer.data());
  buffer.shrink_to_fit();
  return fit::ok(std::move(buffer));
}

}  // namespace zxdump::internal
