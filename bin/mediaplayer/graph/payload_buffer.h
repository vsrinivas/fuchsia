// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_MEDIAPLAYER_GRAPH_PAYLOAD_BUFFER_H_
#define GARNET_BIN_MEDIAPLAYER_GRAPH_PAYLOAD_BUFFER_H_

#include <fbl/recycler.h>
#include <fbl/ref_counted.h>
#include <fbl/ref_ptr.h>
#include <lib/fit/function.h>
#include <lib/zx/vmo.h>
#include <memory>
#include "lib/fxl/logging.h"

namespace media_player {

// A buffer used to hold a packet payload.
//
// A |PayloadBuffer| instance is managed using |fbl::RefPtr| and has an
// associated recycler, which is responsible for freeing the memory that the
// |PayloadBuffer| encapsulates. When the last reference to a |PayloadBuffer|
// is dropped, the recycler is called.
class PayloadBuffer final : public fbl::RefCounted<PayloadBuffer>,
                            public fbl::Recyclable<PayloadBuffer> {
 public:
  // All payload buffers must be aligned on |kByteAlignment|-byte boundaries.
  static constexpr size_t kByteAlignment = 32;

  // Returns the smallest multiple of |kByteAlignment| that is no smaller than
  // |size|.
  static size_t AlignUp(size_t size) {
    return size = (size + kByteAlignment - 1) & ~(kByteAlignment - 1);
  }

  // Indicates whether |buffer| is aligned to |kByteAlignment| bytes.
  static bool IsAligned(void* buffer) {
    return (reinterpret_cast<uintptr_t>(buffer) & (kByteAlignment - 1)) == 0;
  }

  // Function type used to recycle a |PayloadBuffer|. The |PayloadBuffer|
  // deletes itself, so the recycler should not attempt to delete it.
  using Recycler = fit::function<void(PayloadBuffer*)>;

  // Function type used for |BeforeRecycling|.
  using Action = fit::function<void(PayloadBuffer*)>;

  // Creates a new |PayloadBuffer|. |size| may not be 0, and |data| may not be
  // nullptr.
  // TODO(dalesat): Support null data for payloads that can't be mapped.
  static fbl::RefPtr<PayloadBuffer> Create(uint64_t size, void* data,
                                           Recycler recycler);

  ~PayloadBuffer();

  // Returns the size in bytes of the buffer, which will never be 0.
  uint64_t size() const { return size_; }

  // Returns a pointer to the buffer, which will never be nullptr.
  void* data() const { return data_; }

  // Registers a function to be called prior to recycling. This method may only
  // be called once on a given instance. An |Action| should not hold a reference
  // to the |PayloadBuffer|, because this would produce a circular reference,
  // and the |PayloadBuffer| would never be released.
  void BeforeRecycling(Action action);

 private:
  PayloadBuffer(uint64_t size, void* data, Recycler recycler);

  void fbl_recycle();

  uint64_t size_;
  void* data_;
  Recycler recycler_;
  Action before_recycling_;

  friend class fbl::Recyclable<PayloadBuffer>;
};

}  // namespace media_player

#endif  // GARNET_BIN_MEDIAPLAYER_GRAPH_PAYLOAD_BUFFER_H_
