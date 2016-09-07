// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/zip/memory_io.h"

#include <stdio.h>
#include <string.h>

#include <algorithm>
#include <vector>

namespace zip {
namespace internal {
namespace {

struct FileStream {
  std::vector<char>* buffer = nullptr;
  size_t offset = 0u;

  char* begin() { return buffer->data(); }
  size_t size() { return buffer->size(); }
};

void* OpenFile(void* opaque, const char* filename, int mode) {
  FileStream* fstream = new FileStream();
  fstream->buffer = static_cast<std::vector<char>*>(opaque);
  return fstream;
}

unsigned long ReadFile(void* opaque,
                       void* stream,
                       void* buffer,
                       unsigned long size) {
  FileStream* fstream = static_cast<FileStream*>(stream);
  unsigned long bytes_read = std::min(size, fstream->size() - fstream->offset);
  memcpy(buffer, fstream->begin() + fstream->offset, bytes_read);
  fstream->offset += bytes_read;
  return bytes_read;
}

unsigned long WriteFile(void* opaque,
                        void* stream,
                        const void* buffer,
                        unsigned long size) {
  FileStream* fstream = static_cast<FileStream*>(stream);
  size_t end = fstream->offset + size;
  if (end > fstream->size())
    fstream->buffer->resize(end);
  memcpy(fstream->begin() + fstream->offset, buffer, size);
  fstream->offset += size;
  return size;
}

long TellFile(void* opaque, void* stream) {
  FileStream* fstream = static_cast<FileStream*>(stream);
  return fstream->offset;
}

long SeekFile(void* opaque, void* stream, unsigned long offset, int origin) {
  FileStream* fstream = static_cast<FileStream*>(stream);
  switch (origin) {
    case SEEK_SET:
      // It's possible we should expand the file if we're in write mode, but we
      // just pretend the disk is full and return -1 instead because the
      // expansion behavior doesn't appear to be required by zlib.
      if (offset > fstream->size())
        break;
      fstream->offset = offset;
      return 0;
    case SEEK_CUR: {
      size_t target = fstream->offset + offset;
      // It's possible we should expand the file if we're in write mode, but we
      // just pretend the disk is full and return -1 instead because the
      // expansion behavior doesn't appear to be required by zlib.
      if (target > fstream->size())
        break;
      fstream->offset = target;
      return 0;
    }
    case SEEK_END:
      if (offset > fstream->size())
        break;
      fstream->offset = fstream->size() - offset;
      return 0;
    default:
      break;
  }
  return -1;
}

int CloseFile(void* opaque, void* stream) {
  FileStream* fstream = static_cast<FileStream*>(stream);
  delete fstream;
  return 0;
}

int ErrorFile(void* opaque, void* stream) {
  return 0;
}

}  // namespace

const zlib_filefunc_def kMemoryIO = {
    &OpenFile, &ReadFile,  &WriteFile, &TellFile,
    &SeekFile, &CloseFile, &ErrorFile, nullptr,
};

}  // namespace internal
}  // namespace zip
