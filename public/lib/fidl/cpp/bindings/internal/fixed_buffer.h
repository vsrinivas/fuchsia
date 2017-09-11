// Copyright 2014 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FIDL_CPP_BINDINGS_INTERNAL_FIXED_BUFFER_H_
#define LIB_FIDL_CPP_BINDINGS_INTERNAL_FIXED_BUFFER_H_

#include "lib/fidl/cpp/bindings/internal/buffer.h"
#include "lib/fxl/macros.h"

namespace fidl {
namespace internal {

// FixedBuffer provides a simple way to allocate objects within a fixed chunk
// of memory. Objects are allocated by calling the |Allocate| method, which
// extends the buffer accordingly. Objects allocated in this way are not freed
// explicitly. Instead, they remain valid so long as the FixedBuffer remains
// valid.  The Leak method may be used to steal the underlying memory from the
// FixedBuffer.
//
// Typical usage:
//
//   {
//     FixedBuffer buf(8 + 8);
//
//     int* a = static_cast<int*>(buf->Allocate(sizeof(int)));
//     *a = 2;
//
//     double* b = static_cast<double*>(buf->Allocate(sizeof(double)));
//     *b = 3.14f;
//
//     void* data = buf.Leak();
//     Process(data);
//
//     free(data);
//   }

class FixedBuffer : public Buffer {
 public:
  FixedBuffer();

  // |size| should be aligned using internal::Align.
  void Initialize(void* memory, size_t size);

  size_t size() const { return size_; }

  // Returns the number of bytes used so far.
  // TODO(vardhan): Introduce this method in |Buffer|? Doesn't seem necessary.
  size_t BytesUsed() const { return cursor_; }

  // Grows the buffer by |num_bytes| and returns a pointer to the start of the
  // addition. The resulting address is 8-byte aligned, and the content of the
  // memory is zero-filled.
  // TODO(vardhan): Allocate() should safely fail if we run out of buffer space.
  // This will allow us to, e.g, fail when trying to consume a buffer to
  // serialize into, and return an insufficient space error. Currently, there
  // are consumers of FixedBuffer that rely on it CHECK-failing.
  void* Allocate(size_t num_bytes) override;

 protected:
  char* ptr_;
  size_t cursor_;
  size_t size_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FixedBuffer);
};

class FixedBufferForTesting : public FixedBuffer {
 public:
  explicit FixedBufferForTesting(size_t size);
  ~FixedBufferForTesting() override;

  // Returns the internal memory owned by the Buffer to the caller. The Buffer
  // relinquishes its pointer, effectively resetting the state of the Buffer
  // and leaving the caller responsible for freeing the returned memory address
  // when no longer needed.
  void* Leak();

 private:
  FXL_DISALLOW_COPY_AND_ASSIGN(FixedBufferForTesting);
};

}  // namespace internal
}  // namespace fidl

#endif  // LIB_FIDL_CPP_BINDINGS_INTERNAL_FIXED_BUFFER_H_
