// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include <magenta/types.h>
#include <mx/vmar.h>
#include <mx/vmo.h>

#include "lib/ftl/logging.h"

namespace media {

// MappedSharedBuffer simplifies the use of shared buffers by taking care of
// mapping/unmapping and by providing offset/pointer translation. It can be
// used when the caller wants to allocate its own buffer (InitNew) and when
// the caller needs to use a buffer supplied by another party (InitFromVmo).
// It can be used by itself when regions of the buffer are allocated by another
// party. If the caller needs to allocate regions, SharedMediaBufferAllocator,
// which is derived from MappedSharedBuffer, provides allocation semantics
// using FifoAllocator.
class MappedSharedBuffer {
 public:
  MappedSharedBuffer();

  virtual ~MappedSharedBuffer();

  // Initializes by creating a new shared buffer of the indicated size.
  mx_status_t InitNew(uint64_t size, uint32_t map_flags);

  // Initializes from a vmo to an existing shared buffer.
  mx_status_t InitFromVmo(mx::vmo vmo, uint32_t map_flags);

  // Indicates whether the buffer is initialized.
  bool initialized() const;

  // Shuts down the buffer.
  void Reset();

  // Gets the size of the buffer.
  uint64_t size() const;

  // Gets a duplicate vmo for the shared buffer.
  mx::vmo GetDuplicateVmo(mx_rights_t rights) const;

  // Validates an offset and size.
  bool Validate(uint64_t offset, uint64_t size);

  // Translates an offset into a pointer.
  void* PtrFromOffset(uint64_t offset) const;

  // Translates a pointer into an offset.
  uint64_t OffsetFromPtr(void* payload_ptr) const;

 protected:
  mx_status_t InitInternal(mx::vmo vmo, uint32_t map_flags);

  // Does nothing. Called when initialization is complete. Subclasses may
  // override.
  virtual void OnInit();

 private:
  // Pointer to the mapped buffer.
  uint8_t* buffer_ptr_;

  // Size of the shared buffer.
  uint64_t size_;

  // VMO to shared buffer when initialized with InitFromVmo.
  mx::vmo vmo_;
};

}  // namespace media
